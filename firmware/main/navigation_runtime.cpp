#include "navigation_runtime.hpp"

#include <algorithm>
#include <array>
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
#include "esp_partition.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "bikeos/canvas.hpp"
#include "bikeos/color_renderer.hpp"
#include "bikeos/framebuffer.hpp"
#include "bikeos/gps.hpp"
#include "bikeos/gps_simulator.hpp"
#include "bikeos/map_data.hpp"
#include "bikeos/map_renderer.hpp"
#include "bikeos/map_tiles.hpp"
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

protocol::RunState protocol_run_state(const NavigationRunState state)
{
    switch (state) {
    case NavigationRunState::running: return protocol::RunState::running;
    case NavigationRunState::paused: return protocol::RunState::paused;
    case NavigationRunState::stopped: return protocol::RunState::stopped;
    case NavigationRunState::arrived: return protocol::RunState::arrived;
    case NavigationRunState::not_started: return protocol::RunState::not_started;
    }
    return protocol::RunState::not_started;
}

std::uint64_t uptime_ms()
{
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

std::span<const std::uint8_t> embedded_asset(const std::uint8_t* begin, const std::uint8_t* end)
{
    return {begin, static_cast<std::size_t>(end - begin)};
}

// Decoded tile features are thousands of small vectors and strings. Below
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL they would all land in internal RAM,
// which BLE, task stacks and DMA need, so steer them to PSRAM while the tile
// window rebuilds.
class PsramForSmallAllocations {
public:
    PsramForSmallAllocations()
    {
#if CONFIG_SPIRAM_USE_MALLOC
        heap_caps_malloc_extmem_enable(16);
#endif
    }
    ~PsramForSmallAllocations()
    {
#if CONFIG_SPIRAM_USE_MALLOC
        heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
#endif
    }
    PsramForSmallAllocations(const PsramForSmallAllocations&) = delete;
    PsramForSmallAllocations& operator=(const PsramForSmallAllocations&) = delete;
};

}  // namespace

struct NavigationRuntime::State {
    mutable std::mutex mutex;
    // Static map: the embedded demo map, or one that came with a BLE bundle.
    MapDataset map;
    // City map from the "map" flash partition, memory-mapped; only the 3x3
    // tiles around the rider are decoded, into tile_window.map().
    TileArchive tiles;
    TileWindow tile_window;
    esp_partition_mmap_handle_t tiles_mapping{};
    bool use_tiles{};
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
    std::int32_t eta_minutes_local{-1};
    bool reroute_pending{};

    const MapDataset& active_map() const { return use_tiles ? tile_window.map() : map; }
    // Loads the tiles around `position` when the rider has moved into another tile.
    void update_tiles(GeoPointE7 position);
    bool open_tile_partition();
};

bool NavigationRuntime::State::open_tile_partition()
{
    const esp_partition_t* partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "map");
    if (partition == nullptr) {
        ESP_LOGW(kTag, "No \"map\" partition; using the embedded demo map");
        return false;
    }
    std::array<std::uint8_t, TileArchive::header_size> header{};
    if (esp_partition_read(partition, 0, header.data(), header.size()) != ESP_OK) return false;
    const std::size_t size = TileArchive::declared_size(header);
    if (size == 0 || size > partition->size) {
        ESP_LOGW(kTag, "Map partition is empty or not BTIL; using the embedded demo map (flash it with idf.py map-flash)");
        return false;
    }
    const void* mapped = nullptr;
    if (esp_partition_mmap(partition, 0, size, ESP_PARTITION_MMAP_DATA, &mapped, &tiles_mapping) != ESP_OK) {
        ESP_LOGE(kTag, "Could not memory-map %u bytes of the map partition", static_cast<unsigned>(size));
        return false;
    }
    if (!tiles.open({static_cast<const std::uint8_t*>(mapped), size})) {
        ESP_LOGE(kTag, "Map partition failed validation (header or index CRC); using the embedded demo map");
        esp_partition_munmap(tiles_mapping);
        tiles_mapping = {};
        return false;
    }
    tile_window.set_archive(&tiles);
    const auto& grid = tiles.grid();
    ESP_LOGI(kTag, "City map: %u tiles, %ux%u grid, %u bytes at flash 0x%" PRIx32,
             static_cast<unsigned>(tiles.tile_count()), grid.columns, grid.rows, static_cast<unsigned>(size),
             partition->address);
    return true;
}

void NavigationRuntime::State::update_tiles(const GeoPointE7 position)
{
    if (!use_tiles || !valid_coordinate(position)) return;
    const std::int64_t start_us = esp_timer_get_time();
    const std::uint32_t decoded_before = tile_window.tiles_decoded();
    bool changed = false;
    {
        PsramForSmallAllocations psram;
        changed = tile_window.update(position);
    }
    if (!changed) return;
    const auto centre = *tile_window.centre();
    ESP_LOGI(kTag, "Map tiles around %u,%u: decoded %" PRIu32 " (failed %" PRIu32 "), %u roads %u areas, %.1f ms, "
             "internal free %u",
             centre.column, centre.row, tile_window.tiles_decoded() - decoded_before, tile_window.tiles_failed(),
             static_cast<unsigned>(tile_window.map().roads.size()), static_cast<unsigned>(tile_window.map().areas.size()),
             static_cast<double>(esp_timer_get_time() - start_us) / 1000.0,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
}

bool NavigationRuntime::initialize()
{
    auto state = std::make_unique<State>();
    const auto map_bytes = embedded_asset(amsterdam_demo_bmap_start, amsterdam_demo_bmap_end);
    const auto route_bytes = embedded_asset(amsterdam_demo_brte_start, amsterdam_demo_brte_end);
    // Prefer the city map in flash; the embedded demo map is the fallback
    // when the partition is missing, blank or corrupt.
    state->use_tiles = state->open_tile_partition();
    if (!state->use_tiles && !decode_map(map_bytes, state->map)) {
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
    state->update_tiles(state->route.points.front());
#if CONFIG_BIKEOS_GPS_SIM
    if (!state->simulator.set_route(state->route)) return false;
    state->simulation_paused = true;
    state->navigation.set_run_state(NavigationRunState::not_started);
    // Render the initial route even though the simulator waits for Start.
    // Without this snapshot, the color TFT has nothing to draw while the
    // simulator is paused in the ready-to-ride state.
#if CONFIG_BIKEOS_DISPLAY_ILI9341 && !CONFIG_BIKEOS_DISPLAY_TEST_PATTERN
    state->latest = state->navigation.snapshot();
    state->have_latest = true;
#endif
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
             "OSM map ready (%s): %u roads %u points; route=%" PRIu32 " %.0fm %u points",
             state->use_tiles ? "city tiles" : "embedded demo",
             static_cast<unsigned>(state->active_map().roads.size()),
             static_cast<unsigned>(state->active_map().point_count()),
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
    if (navigation.run_state == NavigationRunState::arrived) state_->simulation_paused = true;
    // Tile loading runs here, once a second, not in render_frame(): crossing
    // a tile edge decodes at most three new tiles (a few ms) and delays one frame.
    state_->update_tiles(navigation.gps_valid ? navigation.position : navigation.matched_position);
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
                            state_->active_map(),
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
    state_->color_renderer.render(*state_->canvas, state_->active_map(), state_->route.points, state_->latest,
                                  {.utc_epoch_ms = utc_now,
                                   .eta_minutes_local = state_->eta_minutes_local,
                                   .now_ms = frame_ms});
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
#if CONFIG_BIKEOS_DISPLAY_ILI9341 && !CONFIG_BIKEOS_DISPLAY_TEST_PATTERN
    // Keep the Bluetooth glyph current even while navigation is paused.
    state_->latest = state_->navigation.snapshot();
    state_->have_latest = true;
#endif
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
    result.speed_cms = navigation.run_state == NavigationRunState::running ? 450 : 0;
#endif
    result.matched_point_index = static_cast<std::uint16_t>(std::min<std::size_t>(navigation.matched_segment, 65535));
    result.run_state = protocol_run_state(navigation.run_state);
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
    const bool route_only = map_bytes.empty();
    if ((!route_only && !decode_map(map_bytes, candidate_map)) || !decode_route(route_bytes, candidate_route) ||
        candidate_route.route_id != begin.route_id) {
        return protocol::ResultCode::invalid_bundle;
    }
    // Custom routes are route-only bundles. Require every route point to be
    // inside a real BTIL tile so we never accept a route and silently render
    // stale or unrelated demo streets around it.
    if (route_only) {
        if (!state_->tiles.is_open() ||
            !std::all_of(candidate_route.points.begin(), candidate_route.points.end(), [&](const GeoPointE7 point) {
                const auto key = state_->tiles.grid().key_for(point);
                return state_->tiles.grid().contains(point) && key.has_value() && state_->tiles.find(*key) != nullptr;
            })) {
            ESP_LOGW(kTag, "Route %" PRIu32 " is outside BTIL map coverage", candidate_route.route_id);
            return protocol::ResultCode::invalid_bundle;
        }
    }
    std::scoped_lock lock(state_->mutex);
    if (!state_->navigation.set_route(candidate_route)) return protocol::ResultCode::invalid_bundle;
    candidate_map.revision = state_->map.revision + 1;
    state_->map = std::move(candidate_map);  // empty when the tiles take over
    state_->route = std::move(candidate_route);
    state_->use_tiles = route_only && state_->tiles.is_open();
    if (state_->use_tiles) {
        state_->update_tiles(state_->route.points.front());
    } else {
        state_->tile_window.clear();  // free the decoded tiles while a bundled map is shown
    }
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
    state_->navigation.set_run_state(NavigationRunState::not_started);
#else
    // Real GPS mode is armed but idle after transfer. The rider can load the
    // route indoors, press Start before leaving, and the first valid outdoor
    // fix will move the route from WAITING_GPS to NAVIGATING.
    state_->navigation.set_run_state(NavigationRunState::not_started);
#endif
#if CONFIG_BIKEOS_DISPLAY_ILI9341 && !CONFIG_BIKEOS_DISPLAY_TEST_PATTERN
    state_->latest = state_->navigation.snapshot();
    state_->have_latest = true;
#endif
    state_->reroute_pending = false;
    state_->eta_minutes_local = -1;
    ESP_LOGI(kTag, "Activated BLE bundle=%" PRIu32 " route=%" PRIu32, bundle_id, begin.route_id);
    return protocol::ResultCode::ok;
}

protocol::ResultCode NavigationRuntime::handle_navigation_command(const protocol::MessageType command,
                                                                  const std::span<const std::uint8_t> payload)
{
    if (!state_) return protocol::ResultCode::invalid_bundle;
    std::scoped_lock lock(state_->mutex);
    switch (command) {
        case protocol::MessageType::start_navigation:
        case protocol::MessageType::resume_navigation:
            if (state_->route.route_id == 0) return protocol::ResultCode::invalid_bundle;
            if (state_->navigation.snapshot().run_state == NavigationRunState::arrived) {
                return protocol::ResultCode::invalid_bundle;
            }
#if CONFIG_BIKEOS_GPS_SIM
            state_->simulation_paused = false;
#endif
            state_->navigation.set_run_state(NavigationRunState::running);
#if CONFIG_BIKEOS_DISPLAY_ILI9341 && !CONFIG_BIKEOS_DISPLAY_TEST_PATTERN
            state_->latest = state_->navigation.snapshot();
            state_->have_latest = true;
#endif
            return protocol::ResultCode::ok;
        case protocol::MessageType::pause_navigation:
#if CONFIG_BIKEOS_GPS_SIM
            state_->simulation_paused = true;
#endif
            state_->navigation.set_run_state(NavigationRunState::paused);
#if CONFIG_BIKEOS_DISPLAY_ILI9341 && !CONFIG_BIKEOS_DISPLAY_TEST_PATTERN
            state_->latest = state_->navigation.snapshot();
            state_->have_latest = true;
#endif
            return protocol::ResultCode::ok;
        case protocol::MessageType::stop_navigation:
        case protocol::MessageType::cancel_navigation:
#if CONFIG_BIKEOS_GPS_SIM
            state_->simulation_paused = true;
#endif
            state_->navigation.set_run_state(NavigationRunState::stopped);
#if CONFIG_BIKEOS_DISPLAY_ILI9341 && !CONFIG_BIKEOS_DISPLAY_TEST_PATTERN
            state_->latest = state_->navigation.snapshot();
            state_->have_latest = true;
#endif
            return protocol::ResultCode::ok;
        case protocol::MessageType::restart_navigation:
            if (state_->route.route_id == 0) return protocol::ResultCode::invalid_bundle;
#if CONFIG_BIKEOS_GPS_SIM
            state_->simulator.set_route(state_->route);
            state_->simulator.set_speed_mps(kDemoSpeedMps);
            state_->simulator.set_events({
                {state_->route.total_distance_m * 0.42F, state_->route.total_distance_m * 0.50F,
                 SimulationEventType::off_route, 50.0F},
                {state_->route.total_distance_m * 0.68F, state_->route.total_distance_m * 0.73F,
                 SimulationEventType::gps_loss, 0.0F},
            });
#endif
            state_->filter.reset();
            if (!state_->navigation.set_route(state_->route)) return protocol::ResultCode::invalid_bundle;
#if CONFIG_BIKEOS_GPS_SIM
            state_->simulation_paused = false;
#endif
            state_->navigation.set_run_state(NavigationRunState::running);
            state_->reroute_pending = false;
#if CONFIG_BIKEOS_DISPLAY_ILI9341 && !CONFIG_BIKEOS_DISPLAY_TEST_PATTERN
            state_->latest = state_->navigation.snapshot();
            state_->have_latest = true;
#endif
            return protocol::ResultCode::ok;
#if CONFIG_BIKEOS_GPS_SIM
        case protocol::MessageType::restore_navigation: {
            protocol::NavigationRestore restore{};
            if (!protocol::decode_navigation_restore(payload, restore) ||
                restore.route_id != state_->route.route_id) {
                return protocol::ResultCode::invalid_bundle;
            }
            if (!state_->simulator.set_route(state_->route)) return protocol::ResultCode::invalid_bundle;
            state_->simulator.set_speed_mps(kDemoSpeedMps);
            state_->simulator.set_events({
                {state_->route.total_distance_m * 0.42F, state_->route.total_distance_m * 0.50F,
                 SimulationEventType::off_route, 50.0F},
                {state_->route.total_distance_m * 0.68F, state_->route.total_distance_m * 0.73F,
                 SimulationEventType::gps_loss, 0.0F},
            });
            state_->simulator.set_progress_m(static_cast<float>(restore.progress_cm) / 100.0F);
            state_->filter.reset();
            if (!state_->navigation.set_route(state_->route)) return protocol::ResultCode::invalid_bundle;
            // Take one zero-duration simulator sample at the saved offset so
            // the navigation snapshot, maneuver, map tiles, and TFT marker all
            // agree before the user resumes riding.
            state_->navigation.set_run_state(NavigationRunState::running);
            const auto now = uptime_ms();
            if (const auto fix = state_->simulator.step(now, 0.0F)) {
                state_->navigation.update(fix, state_->phone_connected, now);
            }
            const auto requested = static_cast<NavigationRunState>(restore.run_state);
            if (requested == NavigationRunState::running) {
                // A power-cycle must never start the simulator unattended.
                state_->navigation.set_run_state(NavigationRunState::paused);
                state_->simulation_paused = true;
            } else {
                state_->navigation.set_run_state(requested);
                state_->simulation_paused = requested != NavigationRunState::running;
            }
            state_->update_tiles(state_->navigation.snapshot().matched_position);
            state_->reroute_pending = false;
#if CONFIG_BIKEOS_DISPLAY_ILI9341 && !CONFIG_BIKEOS_DISPLAY_TEST_PATTERN
            state_->latest = state_->navigation.snapshot();
            state_->have_latest = true;
#endif
            ESP_LOGI(kTag, "Restored route=%" PRIu32 " progress=%.1fm state=%u",
                     restore.route_id, static_cast<double>(restore.progress_cm) / 100.0,
                     static_cast<unsigned>(state_->navigation.snapshot().run_state));
            return protocol::ResultCode::ok;
        }
#endif
        case protocol::MessageType::set_route_eta: {
            protocol::RouteEta eta{};
            if (!protocol::decode_route_eta(payload, eta) || eta.route_id != state_->route.route_id) {
                return protocol::ResultCode::invalid_bundle;
            }
            state_->eta_minutes_local = eta.eta_minutes_local;
#if CONFIG_BIKEOS_DISPLAY_ILI9341 && !CONFIG_BIKEOS_DISPLAY_TEST_PATTERN
            state_->latest = state_->navigation.snapshot();
            state_->have_latest = true;
#endif
            ESP_LOGI(kTag, "Phone ETA route=%" PRIu32 " %02u:%02u", eta.route_id,
                     static_cast<unsigned>(eta.eta_minutes_local / 60),
                     static_cast<unsigned>(eta.eta_minutes_local % 60));
            return protocol::ResultCode::ok;
        }
        case protocol::MessageType::trigger_off_route: {
#if CONFIG_BIKEOS_GPS_SIM
            if (state_->route.route_id == 0) return protocol::ResultCode::invalid_bundle;
            const float start = state_->simulator.progress_m() + 1.0F;
            const float end = std::min(state_->route.total_distance_m, start + 45.0F);
            state_->simulator.set_events({{start, end, SimulationEventType::off_route, 50.0F}});
            return protocol::ResultCode::ok;
#else
            return protocol::ResultCode::unauthorized;
#endif
        }
        default:
            return protocol::ResultCode::invalid_frame;
    }
}

}  // namespace bikeos
