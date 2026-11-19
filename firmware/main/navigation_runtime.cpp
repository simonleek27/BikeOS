#include "navigation_runtime.hpp"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "bikeos/canvas.hpp"
#include "bikeos/color_renderer.hpp"
#include "bikeos/framebuffer.hpp"
#include "bikeos/gps.hpp"
#include "bikeos/gps_simulator.hpp"
#include "bikeos/map_data.hpp"
#include "bikeos/map_renderer.hpp"
#include "bikeos/navigation.hpp"
#include "bikeos/protocol.hpp"
#include "ili9341_display.hpp"
#include "st7565_display.hpp"
#include "uart_gps_source.hpp"

extern const std::uint8_t amsterdam_demo_bmap_start[] asm("_binary_amsterdam_demo_bmap_start");
extern const std::uint8_t amsterdam_demo_bmap_end[] asm("_binary_amsterdam_demo_bmap_end");
extern const std::uint8_t amsterdam_demo_brte_start[] asm("_binary_amsterdam_demo_brte_start");
extern const std::uint8_t amsterdam_demo_brte_end[] asm("_binary_amsterdam_demo_brte_end");

namespace bikeos {
namespace {

constexpr char kTag[] = "navigation";
constexpr float kDemoSpeedMps = 4.5F;

std::uint64_t uptime_ms()
{
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

std::span<const std::uint8_t> embedded_asset(const std::uint8_t* begin, const std::uint8_t* end)
{
    return {begin, static_cast<std::size_t>(end - begin)};
}

}  // namespace

struct NavigationRuntime::State {
    mutable std::mutex mutex;
    MapDataset map;
    Route route;
    GpsSimulator simulator{0xB14E05U};
    GpsFilter filter;
    NavigationEngine navigation;
    MapRenderer renderer;
    Framebuffer framebuffer;
#if CONFIG_BIKEOS_GPS_UART
    std::unique_ptr<UartGpsSource> uart_gps;
#endif
#if CONFIG_BIKEOS_DISPLAY_ST7565
    std::unique_ptr<St7565Display> display;
#endif
#if CONFIG_BIKEOS_DISPLAY_ILI9341
    std::unique_ptr<Ili9341Display> ili9341_display;
    std::unique_ptr<Canvas565> canvas;  // 150 KiB, lands in PSRAM
    ColorMapRenderer color_renderer;
    NavigationSnapshot latest;
    bool have_latest{};
    std::uint32_t last_frame_crc{};
    std::uint32_t frames{};
    std::uint64_t frame_window_start_ms{};
    std::int64_t frame_window_render_us{};
    std::int64_t frame_window_flush_us{};
#endif
    // Last GPS wall-clock time and the monotonic time it arrived, for the clock chip.
    std::int64_t utc_epoch_ms{};
    std::uint64_t utc_epoch_received_ms{};
    std::uint32_t step_count{};
    bool simulation_paused{};
    bool phone_connected{};
    bool reroute_pending{};
};

bool NavigationRuntime::initialize()
{
    auto state = std::make_unique<State>();
    const auto map_bytes = embedded_asset(amsterdam_demo_bmap_start, amsterdam_demo_bmap_end);
    const auto route_bytes = embedded_asset(amsterdam_demo_brte_start, amsterdam_demo_brte_end);
    if (!decode_map(map_bytes, state->map)) {
        ESP_LOGE(kTag, "Map validation failed (%u bytes)", static_cast<unsigned>(map_bytes.size()));
        return false;
    }
    if (!decode_route(route_bytes, state->route)) {
        ESP_LOGE(kTag, "Route validation failed (%u bytes)", static_cast<unsigned>(route_bytes.size()));
        return false;
    }
    if (!state->navigation.set_route(state->route)) {
        ESP_LOGE(kTag, "Navigation initialization failed");
        return false;
    }
#if CONFIG_BIKEOS_GPS_SIM
    if (!state->simulator.set_route(state->route)) return false;
    // Keep the demo physically believable: 16.2 km/h is a normal bicycle pace.
    // The previous 18 m/s value made the moving-marker demo traverse the route
    // at motorway speed and made the LCD/phone telemetry hard to inspect.
    state->simulator.set_speed_mps(kDemoSpeedMps);
    state->simulator.set_events({
        {state->route.total_distance_m * 0.42F, state->route.total_distance_m * 0.50F,
         SimulationEventType::off_route, 50.0F},
        {state->route.total_distance_m * 0.68F, state->route.total_distance_m * 0.73F,
         SimulationEventType::gps_loss, 0.0F},
    });
#elif CONFIG_BIKEOS_GPS_UART
    state->uart_gps = std::make_unique<UartGpsSource>(UartGpsConfig{
        .uart_number = CONFIG_BIKEOS_GPS_UART_NUMBER,
        .rx_gpio = CONFIG_BIKEOS_GPS_RX_GPIO,
        .tx_gpio = CONFIG_BIKEOS_GPS_TX_GPIO,
        .baud_rate = CONFIG_BIKEOS_GPS_BAUD,
    });
    if (!state->uart_gps->initialize()) return false;
#else
    ESP_LOGE(kTag, "iPhone BLE GPS transport is not implemented yet");
    return false;
#endif
#if CONFIG_BIKEOS_DISPLAY_ST7565
    state->display = std::make_unique<St7565Display>(St7565Config{
        .spi_host = CONFIG_BIKEOS_LCD_SPI_HOST,
        .mosi_gpio = CONFIG_BIKEOS_LCD_MOSI_GPIO,
        .clock_gpio = CONFIG_BIKEOS_LCD_CLOCK_GPIO,
        .chip_select_gpio = CONFIG_BIKEOS_LCD_CS_GPIO,
        .data_command_gpio = CONFIG_BIKEOS_LCD_A0_GPIO,
        .reset_gpio = CONFIG_BIKEOS_LCD_RESET_GPIO,
        .column_offset = CONFIG_BIKEOS_LCD_COLUMN_OFFSET,
        .contrast = CONFIG_BIKEOS_LCD_CONTRAST,
    });
    if (!state->display->initialize()) return false;
#endif
#if CONFIG_BIKEOS_DISPLAY_ILI9341
    state->ili9341_display = std::make_unique<Ili9341Display>(Ili9341Config{
        .spi_host = CONFIG_BIKEOS_ILI9341_SPI_HOST,
        .mosi_gpio = CONFIG_BIKEOS_ILI9341_MOSI_GPIO,
        .clock_gpio = CONFIG_BIKEOS_ILI9341_CLOCK_GPIO,
        .chip_select_gpio = CONFIG_BIKEOS_ILI9341_CS_GPIO,
        .data_command_gpio = CONFIG_BIKEOS_ILI9341_DC_GPIO,
        .reset_gpio = CONFIG_BIKEOS_ILI9341_RESET_GPIO,
        .clock_hz = CONFIG_BIKEOS_ILI9341_CLOCK_HZ,
    });
    if (!state->ili9341_display->initialize()) return false;
#if CONFIG_BIKEOS_DISPLAY_TEST_PATTERN
    if (!state->ili9341_display->show_test_pattern()) return false;
#endif
    state->canvas = std::make_unique<Canvas565>();
#endif
    ESP_LOGI(kTag,
             "OSM map ready: %u roads %u points %u bytes; route=%" PRIu32 " %.0fm %u points",
             static_cast<unsigned>(state->map.roads.size()),
             static_cast<unsigned>(state->map.point_count()),
             static_cast<unsigned>(map_bytes.size()),
             state->route.route_id,
             state->route.total_distance_m,
             static_cast<unsigned>(state->route.points.size()));
#if CONFIG_BIKEOS_GPS_SIM
    constexpr const char* gps_source = "deterministic simulation at 1Hz";
#else
    constexpr const char* gps_source = "UART NMEA";
#endif
#if CONFIG_BIKEOS_DISPLAY_ST7565
    constexpr const char* display_target = "physical ST7565R SPI";
#elif CONFIG_BIKEOS_DISPLAY_ILI9341
#if CONFIG_BIKEOS_DISPLAY_TEST_PATTERN
    constexpr const char* display_target = "physical ILI9341 SPI test pattern";
#else
    constexpr const char* display_target = "physical ILI9341 SPI";
#endif
#else
    constexpr const char* display_target = "virtual ST7565 framebuffer";
#endif
    ESP_LOGI(kTag, "GPS source: %s; display: %s", gps_source, display_target);
    state_ = state.release();
    return true;
}

void NavigationRuntime::step(const std::uint64_t now_ms)
{
    if (!state_) return;
    std::scoped_lock lock(state_->mutex);
#if CONFIG_BIKEOS_GPS_SIM
    if (state_->simulation_paused) {
        ++state_->step_count;
        return;
    }
#endif
    std::optional<GpsFix> candidate;
#if CONFIG_BIKEOS_GPS_SIM
    candidate = state_->simulator.step(now_ms);
#elif CONFIG_BIKEOS_GPS_UART
    candidate = state_->uart_gps->poll(now_ms);
#endif
    std::optional<GpsFix> accepted;
    if (candidate) accepted = state_->filter.accept(*candidate);
    const auto navigation = state_->navigation.update(accepted, state_->phone_connected, now_ms);
    if (accepted && accepted->utc_epoch_ms != 0) {
        state_->utc_epoch_ms = accepted->utc_epoch_ms;
        state_->utc_epoch_received_ms = uptime_ms();
    }
#if CONFIG_BIKEOS_DISPLAY_ILI9341 && !CONFIG_BIKEOS_DISPLAY_TEST_PATTERN
    // The color TFT is drawn by render_frame(); keep the latest snapshot for it.
    state_->latest = navigation;
    state_->have_latest = true;
    const auto frame_crc = state_->last_frame_crc;
#else
    const GeoPointE7 center = navigation.gps_valid ? navigation.position : navigation.matched_position;
    state_->renderer.render(state_->framebuffer,
                            state_->map,
                            state_->route.points,
                            navigation,
                            {.center = valid_coordinate(center) ? center : state_->route.points.front(),
                             .meters_per_pixel = 4.0F,
                             .heading_up = true,
                             .heading_deg = navigation.heading_deg});
#if CONFIG_BIKEOS_DISPLAY_ST7565
    if (!state_->display->flush(state_->framebuffer)) {
        ESP_LOGE(kTag, "LCD framebuffer transfer failed");
    }
#endif
    const auto frame_crc = crc32(state_->framebuffer.bytes());
#endif
    ++state_->step_count;
    if (accepted) {
        ESP_LOGI(kTag,
                 "fix=%" PRId32 ",%" PRId32 " state=%s progress=%.0fm xtrack=%.1fm next=%.0fm frame=%08" PRIx32,
                 accepted->position.latitude_e7,
                 accepted->position.longitude_e7,
                 navigation_state_name(navigation.state),
                 navigation.route_progress_m,
                 navigation.cross_track_distance_m,
                 navigation.distance_to_maneuver_m,
                 frame_crc);
    } else {
        ESP_LOGW(kTag, "GPS unavailable state=%s frame=%08" PRIx32,
                 navigation_state_name(navigation.state), frame_crc);
    }
    if (navigation.reroute_requested) {
        state_->reroute_pending = true;
        ESP_LOGW(kTag, "Reroute request route=%" PRIu32 " heading=%.1f timestamp=%" PRIu64,
                 navigation.route_id, navigation.heading_deg, now_ms);
    }
}

void NavigationRuntime::render_frame(const std::uint64_t frame_ms)
{
#if CONFIG_BIKEOS_DISPLAY_ILI9341 && !CONFIG_BIKEOS_DISPLAY_TEST_PATTERN
    if (!state_) return;
    std::scoped_lock lock(state_->mutex);
    if (!state_->have_latest) return;
    const std::uint64_t now_ms = uptime_ms();
    const std::int64_t utc_now = state_->utc_epoch_ms == 0
                                     ? 0
                                     : state_->utc_epoch_ms +
                                           static_cast<std::int64_t>(now_ms - state_->utc_epoch_received_ms);
    const std::int64_t render_start_us = esp_timer_get_time();
    state_->color_renderer.render(*state_->canvas, state_->map, state_->route.points, state_->latest,
                                  {.utc_epoch_ms = utc_now, .now_ms = frame_ms});
    const std::int64_t flush_start_us = esp_timer_get_time();
    if (!state_->ili9341_display->flush(*state_->canvas)) {
        ESP_LOGE(kTag, "ILI9341 color frame transfer failed");
    }
    const std::int64_t flush_end_us = esp_timer_get_time();
    const auto pixels = state_->canvas->pixels();
    state_->last_frame_crc = crc32({reinterpret_cast<const std::uint8_t*>(pixels.data()), pixels.size_bytes()});

    state_->frame_window_render_us += flush_start_us - render_start_us;
    state_->frame_window_flush_us += flush_end_us - flush_start_us;
    if (state_->frames++ == 0) state_->frame_window_start_ms = frame_ms;
    const std::uint64_t window_ms = frame_ms - state_->frame_window_start_ms;
    if (window_ms >= 10000) {
        const auto frames = static_cast<double>(state_->frames - 1);
        ESP_LOGI(kTag, "display %.1f fps, render %.0f ms, flush %.0f ms, zoom %.2f m/px",
                 frames * 1000.0 / static_cast<double>(window_ms),
                 static_cast<double>(state_->frame_window_render_us) / 1000.0 / state_->frames,
                 static_cast<double>(state_->frame_window_flush_us) / 1000.0 / state_->frames,
                 static_cast<double>(state_->color_renderer.meters_per_pixel()));
        state_->frames = 0;
        state_->frame_window_render_us = 0;
        state_->frame_window_flush_us = 0;
    }
#else
    (void)frame_ms;
#endif
}

void NavigationRuntime::set_phone_connected(const bool connected)
{
    if (!state_) return;
    std::scoped_lock lock(state_->mutex);
    state_->phone_connected = connected;
    state_->navigation.set_phone_connected(connected);
}

protocol::Status NavigationRuntime::status() const
{
    protocol::Status result;
    if (!state_) return result;
    std::scoped_lock lock(state_->mutex);
    const auto& navigation = state_->navigation.snapshot();
    result.navigation_state = static_cast<protocol::NavigationState>(navigation.state);
#if CONFIG_BIKEOS_GPS_SIM
    result.gps_source = protocol::GpsSource::simulated;
#elif CONFIG_BIKEOS_GPS_UART
    result.gps_source = protocol::GpsSource::uart;
#else
    result.gps_source = protocol::GpsSource::none;
#endif
    result.flags = static_cast<std::uint8_t>((navigation.gps_valid ? 1U : 0U) |
                                             (state_->phone_connected ? 2U : 0U));
#if CONFIG_BIKEOS_DISPLAY_ST7565
    result.flags = static_cast<std::uint8_t>(result.flags | 4U);
#elif CONFIG_BIKEOS_DISPLAY_ILI9341
    result.flags = static_cast<std::uint8_t>(result.flags | 4U);
#endif
    result.latitude_e7 = navigation.position.latitude_e7;
    result.longitude_e7 = navigation.position.longitude_e7;
    result.active_route_id = navigation.route_id;
    result.route_remaining_m = static_cast<std::uint32_t>(std::max(0.0F, navigation.route_remaining_m));
    result.next_maneuver_m = static_cast<std::uint32_t>(std::max(0.0F, navigation.distance_to_maneuver_m));
    result.free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (navigation.heading_valid) {
        result.heading_deg_cdeg = static_cast<std::uint16_t>(std::clamp(navigation.heading_deg, 0.0F, 359.99F) * 100.0F);
    }
#if CONFIG_BIKEOS_GPS_SIM
    result.speed_cms = 450;
#endif
    result.matched_point_index = static_cast<std::uint16_t>(std::min<std::size_t>(navigation.matched_segment, 65535));
    return result;
}

bool NavigationRuntime::consume_reroute_request()
{
    if (!state_) return false;
    std::scoped_lock lock(state_->mutex);
    const bool pending = state_->reroute_pending;
    state_->reroute_pending = false;
    return pending;
}

protocol::ResultCode NavigationRuntime::install_bundle(const std::uint32_t bundle_id,
                                                       const protocol::TransferBegin& begin,
                                                       const std::span<const std::uint8_t> map_bytes,
                                                       const std::span<const std::uint8_t> route_bytes)
{
    if (!state_ || bundle_id == 0 || begin.bundle_id != bundle_id || begin.map_size != map_bytes.size() ||
        begin.route_size != route_bytes.size()) {
        return protocol::ResultCode::invalid_transfer;
    }
    MapDataset candidate_map;
    Route candidate_route;
    if (!decode_map(map_bytes, candidate_map) || !decode_route(route_bytes, candidate_route) ||
        candidate_route.route_id != begin.route_id) {
        return protocol::ResultCode::invalid_bundle;
    }
    std::scoped_lock lock(state_->mutex);
    if (!state_->navigation.set_route(candidate_route)) return protocol::ResultCode::invalid_bundle;
    state_->map = std::move(candidate_map);
    state_->route = std::move(candidate_route);
#if CONFIG_BIKEOS_GPS_SIM
    if (!state_->simulator.set_route(state_->route)) return protocol::ResultCode::invalid_bundle;
    state_->simulator.set_speed_mps(kDemoSpeedMps);
    state_->simulator.set_events({
        {state_->route.total_distance_m * 0.42F, state_->route.total_distance_m * 0.50F,
         SimulationEventType::off_route, 50.0F},
        {state_->route.total_distance_m * 0.68F, state_->route.total_distance_m * 0.73F,
         SimulationEventType::gps_loss, 0.0F},
    });
    state_->simulation_paused = true;
#endif
    state_->reroute_pending = false;
    ESP_LOGI(kTag, "Activated BLE bundle=%" PRIu32 " route=%" PRIu32, bundle_id, begin.route_id);
    return protocol::ResultCode::ok;
}

protocol::ResultCode NavigationRuntime::handle_navigation_command(const protocol::MessageType command)
{
    if (!state_) return protocol::ResultCode::invalid_bundle;
    std::scoped_lock lock(state_->mutex);
#if CONFIG_BIKEOS_GPS_SIM
    switch (command) {
        case protocol::MessageType::start_navigation:
        case protocol::MessageType::resume_navigation:
            if (state_->route.route_id == 0) return protocol::ResultCode::invalid_bundle;
            state_->simulation_paused = false;
            return protocol::ResultCode::ok;
        case protocol::MessageType::pause_navigation:
        case protocol::MessageType::stop_navigation:
        case protocol::MessageType::cancel_navigation:
            state_->simulation_paused = true;
            return protocol::ResultCode::ok;
        case protocol::MessageType::restart_navigation:
            if (state_->route.route_id == 0) return protocol::ResultCode::invalid_bundle;
            state_->simulator.set_route(state_->route);
            state_->simulator.set_speed_mps(kDemoSpeedMps);
            state_->simulator.set_events({
                {state_->route.total_distance_m * 0.42F, state_->route.total_distance_m * 0.50F,
                 SimulationEventType::off_route, 50.0F},
                {state_->route.total_distance_m * 0.68F, state_->route.total_distance_m * 0.73F,
                 SimulationEventType::gps_loss, 0.0F},
            });
            state_->filter.reset();
            if (!state_->navigation.set_route(state_->route)) return protocol::ResultCode::invalid_bundle;
            state_->simulation_paused = false;
            state_->reroute_pending = false;
            return protocol::ResultCode::ok;
        case protocol::MessageType::trigger_off_route: {
            if (state_->route.route_id == 0) return protocol::ResultCode::invalid_bundle;
            const float start = state_->simulator.progress_m() + 1.0F;
            const float end = std::min(state_->route.total_distance_m, start + 45.0F);
            state_->simulator.set_events({{start, end, SimulationEventType::off_route, 50.0F}});
            return protocol::ResultCode::ok;
        }
        default:
            return protocol::ResultCode::invalid_frame;
    }
#else
    (void)command;
    return protocol::ResultCode::unauthorized;
#endif
}

}  // namespace bikeos
