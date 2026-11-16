#include "app.hpp"

#include <cinttypes>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "subsystem_status.hpp"
#include "system_diagnostics.hpp"

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
    ESP_LOGI(kTag, "Initialization complete");

    while (true) {
        vTaskDelay(kHeartbeatPeriod);
        ESP_LOGI(kTag,
                 "Heartbeat uptime=%" PRIu64 "s free_heap=%" PRIu32 "B",
                 uptime_seconds(),
                 heap_caps_get_free_size(MALLOC_CAP_8BIT));
    }
}

}  // namespace bikeos

