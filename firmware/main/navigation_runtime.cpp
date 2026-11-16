#include "navigation_runtime.hpp"

#include <cinttypes>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "bikeos/framebuffer.hpp"
#include "bikeos/gps.hpp"
#include "bikeos/gps_simulator.hpp"
#include "bikeos/map_data.hpp"
#include "bikeos/map_renderer.hpp"
#include "bikeos/navigation.hpp"
#include "st7565_display.hpp"
#include "uart_gps_source.hpp"

extern const std::uint8_t amsterdam_demo_bmap_start[] asm("_binary_amsterdam_demo_bmap_start");
extern const std::uint8_t amsterdam_demo_bmap_end[] asm("_binary_amsterdam_demo_bmap_end");
extern const std::uint8_t amsterdam_demo_brte_start[] asm("_binary_amsterdam_demo_brte_start");
extern const std::uint8_t amsterdam_demo_brte_end[] asm("_binary_amsterdam_demo_brte_end");

namespace bikeos {
namespace {

constexpr char kTag[] = "navigation";

std::span<const std::uint8_t> embedded_asset(const std::uint8_t* begin, const std::uint8_t* end)
{
    return {begin, static_cast<std::size_t>(end - begin)};
}

}  // namespace

struct NavigationRuntime::State {
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
    std::uint32_t step_count{};
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
    state->simulator.set_speed_mps(18.0F);
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
    std::optional<GpsFix> candidate;
#if CONFIG_BIKEOS_GPS_SIM
    candidate = state_->simulator.step(now_ms);
#elif CONFIG_BIKEOS_GPS_UART
    candidate = state_->uart_gps->poll(now_ms);
#endif
    std::optional<GpsFix> accepted;
    if (candidate) accepted = state_->filter.accept(*candidate);
    const auto navigation = state_->navigation.update(accepted, true, now_ms);
    const GeoPointE7 center = navigation.gps_valid ? navigation.position : navigation.matched_position;
    state_->renderer.render(state_->framebuffer,
                            state_->map,
                            state_->route.points,
                            navigation,
                            {.center = valid_coordinate(center) ? center : state_->route.points.front(),
                             .meters_per_pixel = 4.0F,
                             .heading_up = false,
                             .heading_deg = navigation.heading_deg});
#if CONFIG_BIKEOS_DISPLAY_ST7565
    if (!state_->display->flush(state_->framebuffer)) {
        ESP_LOGE(kTag, "LCD framebuffer transfer failed");
    }
#endif
    const auto frame_crc = crc32(state_->framebuffer.bytes());
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
        ESP_LOGW(kTag, "Reroute request route=%" PRIu32 " heading=%.1f timestamp=%" PRIu64,
                 navigation.route_id, navigation.heading_deg, now_ms);
    }
}

}  // namespace bikeos
