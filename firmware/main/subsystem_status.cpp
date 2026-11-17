#include "subsystem_status.hpp"

#include "esp_log.h"

namespace bikeos {
namespace {

constexpr char kTag[] = "subsystems";

}  // namespace

void log_initial_subsystem_status()
{
    ESP_LOGI(kTag, "Memory initialized");
    ESP_LOGI(kTag, "Navigation: route matching + off-route detection");
    ESP_LOGI(kTag, "GPS: deterministic simulation mode (UART parser available)");
    ESP_LOGW(kTag, "Display: virtual 128x64 framebuffer; physical LCD not wired");
    ESP_LOGI(kTag, "BLE: not started");
}

}  // namespace bikeos

