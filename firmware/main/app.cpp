#include "app.hpp"

#include <cinttypes>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "subsystem_status.hpp"
#include "system_diagnostics.hpp"
#include "navigation_runtime.hpp"
#include "ble_peripheral.hpp"

namespace bikeos {
namespace {

constexpr char kTag[] = "bikeos";
constexpr TickType_t kHeartbeatPeriod = pdMS_TO_TICKS(1000);

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
                        [&navigation](const bool connected) { navigation.set_phone_connected(connected); })) {
        ESP_LOGE(kTag, "BLE peripheral initialization failed; continuing without phone transport");
    }
    ESP_LOGI(kTag, "Initialization complete");

    while (true) {
        vTaskDelay(kHeartbeatPeriod);
        navigation.step(uptime_seconds() * 1000ULL);
        const auto status = navigation.status();
        ble.notify_status(status);
        if (navigation.consume_reroute_request()) ble.notify_reroute(status);
        ESP_LOGI(kTag,
                 "Heartbeat uptime=%" PRIu64 "s free_heap=%" PRIu32 "B",
                 uptime_seconds(),
                 heap_caps_get_free_size(MALLOC_CAP_8BIT));
    }
}

}  // namespace bikeos

