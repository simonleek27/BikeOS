#include "subsystem_status.hpp"

#include "esp_log.h"
#include "sdkconfig.h"

namespace bikeos {
namespace {

constexpr char kTag[] = "subsystems";

}  // namespace

void log_initial_subsystem_status()
{
    ESP_LOGI(kTag, "Memory initialized");
    ESP_LOGI(kTag, "Navigation: route matching + off-route detection");
    ESP_LOGI(kTag, "GPS: deterministic simulation mode (UART parser available)");
#if CONFIG_BIKEOS_DISPLAY_ILI9341
#if CONFIG_BIKEOS_DISPLAY_TEST_PATTERN
    ESP_LOGI(kTag, "Display: physical ILI9341 320x240 color-bar test pattern");
#else
    ESP_LOGI(kTag, "Display: physical ILI9341 320x240 SPI");
#endif
#elif CONFIG_BIKEOS_DISPLAY_ST7565
    ESP_LOGI(kTag, "Display: physical ST7565R SPI");
#else
    ESP_LOGW(kTag, "Display: virtual 128x64 framebuffer; physical LCD not wired");
#endif
    ESP_LOGI(kTag, "BLE: not started");
}

}  // namespace bikeos

