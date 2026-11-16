#include "subsystem_status.hpp"

#include "esp_log.h"

namespace bikeos {
namespace {

constexpr char kTag[] = "subsystems";

}  // namespace

void log_initial_subsystem_status()
{
    ESP_LOGI(kTag, "Memory initialized");
    ESP_LOGI(kTag, "Navigation: foundation ready");
    ESP_LOGI(kTag, "GPS: simulation mode");
    ESP_LOGW(kTag, "Display: unavailable (not wired)");
    ESP_LOGI(kTag, "BLE: not started");
}

}  // namespace bikeos

