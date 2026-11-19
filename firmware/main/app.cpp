#include "app.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdint>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "subsystem_status.hpp"
#include "system_diagnostics.hpp"
#include "navigation_runtime.hpp"
#include "ble_peripheral.hpp"

namespace bikeos {
namespace {

constexpr char kTag[] = "bikeos";
constexpr std::uint64_t kStepPeriodMs = 1000;   // navigation and GPS
constexpr std::uint64_t kFramePeriodMs = 100;   // display target, 10 fps

std::uint64_t monotonic_ms()
{
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

}  // namespace

[[noreturn]] void App::run() const
{
    ESP_LOGI(kTag, "Initializing system...");
    log_system_diagnostics();
    log_initial_subsystem_status();
    NavigationRuntime navigation;
    if (!navigation.initialize()) {
        ESP_LOGE(kTag, "Navigation runtime failed; heartbeat remains active");
    }
    BlePeripheral ble;
    if (!ble.initialize([&navigation] { return navigation.status(); },
                        [&navigation](const std::uint32_t bundle_id,
                                      const protocol::TransferBegin& begin,
                                      const std::span<const std::uint8_t> map_bytes,
                                      const std::span<const std::uint8_t> route_bytes) {
                            return navigation.install_bundle(bundle_id, begin, map_bytes, route_bytes);
                        },
                        [&navigation](const bool connected) { navigation.set_phone_connected(connected); },
                        [&navigation](const protocol::MessageType command) {
                            return navigation.handle_navigation_command(command);
                        })) {
        ESP_LOGE(kTag, "BLE peripheral initialization failed; continuing without phone transport");
    }
    ESP_LOGI(kTag, "Initialization complete");

    // Navigation steps once a second; the display draws as often as it can
    // up to kFramePeriodMs so the map glides between steps.
    std::uint64_t next_step_ms = monotonic_ms() + kStepPeriodMs;
    while (true) {
        const std::uint64_t frame_start_ms = monotonic_ms();
        if (frame_start_ms >= next_step_ms) {
            navigation.step(uptime_seconds() * 1000ULL);
            const auto status = navigation.status();
            ble.notify_status(status);
            if (navigation.consume_reroute_request()) ble.notify_reroute(status);
            ESP_LOGI(kTag,
                     "Heartbeat uptime=%" PRIu64 "s free_heap=%" PRIu32 "B",
                     uptime_seconds(),
                     heap_caps_get_free_size(MALLOC_CAP_8BIT));
            next_step_ms += kStepPeriodMs;
            if (next_step_ms <= frame_start_ms) next_step_ms = frame_start_ms + kStepPeriodMs;
        }
        navigation.render_frame(monotonic_ms());
        const std::uint64_t elapsed_ms = monotonic_ms() - frame_start_ms;
        // Always yield at least one tick so lower-priority tasks and the idle watchdog run.
        const std::uint64_t wait_ms = elapsed_ms < kFramePeriodMs ? kFramePeriodMs - elapsed_ms : 0;
        vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(wait_ms)));
    }
}

}  // namespace bikeos

