#include "system_diagnostics.hpp"

#include <cinttypes>
#include <cstddef>

#include "sdkconfig.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"

namespace bikeos {
namespace {

constexpr char kTag[] = "diagnostics";

const char* reset_reason_name(const esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_UNKNOWN:
            return "unknown";
        case ESP_RST_POWERON:
            return "power-on";
        case ESP_RST_EXT:
            return "external-pin";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "interrupt-watchdog";
        case ESP_RST_TASK_WDT:
            return "task-watchdog";
        case ESP_RST_WDT:
            return "watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deep-sleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        case ESP_RST_USB:
            return "usb";
        case ESP_RST_JTAG:
            return "jtag";
        case ESP_RST_EFUSE:
            return "efuse";
        case ESP_RST_PWR_GLITCH:
            return "power-glitch";
        case ESP_RST_CPU_LOCKUP:
            return "cpu-lockup";
        default:
            return "unmapped";
    }
}

}  // namespace

void log_system_diagnostics()
{
    const esp_app_desc_t* app = esp_app_get_description();
    ESP_LOGI(kTag, "BikeOS v%s", app->version);
    ESP_LOGI(kTag, "ESP-IDF %s", app->idf_ver);

    esp_chip_info_t chip{};
    esp_chip_info(&chip);
    const unsigned major_revision = chip.revision / 100;
    const unsigned minor_revision = chip.revision % 100;
    ESP_LOGI(kTag,
             "Target=%s cores=%u silicon=v%u.%u Wi-Fi=%s BLE=%s",
             CONFIG_IDF_TARGET,
             chip.cores,
             major_revision,
             minor_revision,
             (chip.features & CHIP_FEATURE_WIFI_BGN) ? "yes" : "no",
             (chip.features & CHIP_FEATURE_BLE) ? "yes" : "no");

    std::uint32_t flash_size = 0;
    const esp_err_t flash_result = esp_flash_get_size(nullptr, &flash_size);
    if (flash_result == ESP_OK) {
        ESP_LOGI(kTag, "Flash=%" PRIu32 " bytes (%" PRIu32 " MiB)",
                 flash_size, flash_size / (1024U * 1024U));
    } else {
        ESP_LOGE(kTag, "Unable to read flash size: %s", esp_err_to_name(flash_result));
    }

    if (esp_psram_is_initialized()) {
        const std::size_t psram_size = esp_psram_get_size();
        ESP_LOGI(kTag, "PSRAM=%u bytes (%u MiB)",
                 static_cast<unsigned>(psram_size),
                 static_cast<unsigned>(psram_size / (1024U * 1024U)));
    } else {
        ESP_LOGW(kTag, "PSRAM is not initialized");
    }

    ESP_LOGI(kTag, "Reset reason=%s", reset_reason_name(esp_reset_reason()));
    ESP_LOGI(kTag,
             "Heap free=%" PRIu32 "B minimum=%" PRIu32 "B largest=%" PRIu32 "B",
             heap_caps_get_free_size(MALLOC_CAP_8BIT),
             heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

std::uint64_t uptime_seconds()
{
    return static_cast<std::uint64_t>(esp_timer_get_time()) / 1'000'000ULL;
}

}  // namespace bikeos

