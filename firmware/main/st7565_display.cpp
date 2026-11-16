#include "st7565_display.hpp"

#include <array>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace bikeos {
namespace {
constexpr char kTag[] = "st7565";
}

St7565Display::St7565Display(St7565Config config) : config_(config) {}

St7565Display::~St7565Display()
{
    if (device_) spi_bus_remove_device(static_cast<spi_device_handle_t>(device_));
    if (bus_initialized_) spi_bus_free(static_cast<spi_host_device_t>(config_.spi_host));
}

bool St7565Display::initialize()
{
    if (initialized_) return true;
    if (config_.mosi_gpio < 0 || config_.clock_gpio < 0 || config_.chip_select_gpio < 0 ||
        config_.data_command_gpio < 0 || config_.reset_gpio < 0 || config_.clock_hz <= 0) {
        ESP_LOGE(kTag, "Refusing to use unverified LCD GPIO configuration");
        return false;
    }

    const gpio_config_t output_config{
        .pin_bit_mask = (1ULL << config_.data_command_gpio) | (1ULL << config_.reset_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&output_config) != ESP_OK) return false;

    const spi_bus_config_t bus_config{
        .mosi_io_num = config_.mosi_gpio,
        .miso_io_num = -1,
        .sclk_io_num = config_.clock_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .data_io_default_level = false,
        .max_transfer_sz = Framebuffer::width,
        .flags = 0,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = 0,
    };
    const auto host = static_cast<spi_host_device_t>(config_.spi_host);
    if (spi_bus_initialize(host, &bus_config, SPI_DMA_DISABLED) != ESP_OK) return false;
    bus_initialized_ = true;

    spi_device_interface_config_t device_config{};
    device_config = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0,
        .clock_source = SPI_CLK_SRC_DEFAULT,
        .duty_cycle_pos = 128,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = config_.clock_hz,
        .input_delay_ns = 0,
        .sample_point = SPI_SAMPLING_POINT_PHASE_0,
        .spics_io_num = config_.chip_select_gpio,
        .flags = 0,
        .queue_size = 1,
        .pre_cb = nullptr,
        .post_cb = nullptr,
    };
    spi_device_handle_t handle{};
    if (spi_bus_add_device(host, &device_config, &handle) != ESP_OK) return false;
    device_ = handle;

    gpio_set_level(static_cast<gpio_num_t>(config_.reset_gpio), 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(static_cast<gpio_num_t>(config_.reset_gpio), 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    const std::array<std::uint8_t, 10> init{
        0xAE,                                      // display off
        static_cast<std::uint8_t>(config_.reverse_adc ? 0xA1 : 0xA0),
        static_cast<std::uint8_t>(config_.reverse_com ? 0xC8 : 0xC0),
        0xA2,                                      // 1/9 bias
        0x2F,                                      // booster, regulator, follower on
        0x26,                                      // regulator resistor ratio
        0x40,                                      // start line 0
        0xA4,                                      // normal RAM display
        0xA6,                                      // non-inverted
        0xAF,                                      // display on
    };
    for (const auto value : init) if (!command(value)) return false;
    if (!command(0x81) || !command(config_.contrast & 0x3F)) return false;
    initialized_ = true;
    ESP_LOGI(kTag, "ST7565R initialized; physical orientation/contrast must be validated on-panel");
    return true;
}

bool St7565Display::flush(const Framebuffer& framebuffer)
{
    if (!initialized_) return false;
    const auto bytes = framebuffer.bytes();
    for (std::uint8_t page = 0; page < 8; ++page) {
        const std::uint8_t column = config_.column_offset;
        if (!command(static_cast<std::uint8_t>(0xB0 | page)) ||
            !command(static_cast<std::uint8_t>(0x10 | ((column >> 4) & 0x0F))) ||
            !command(static_cast<std::uint8_t>(column & 0x0F)) ||
            !data(bytes.data() + static_cast<std::size_t>(page) * Framebuffer::width,
                  Framebuffer::width)) return false;
    }
    return true;
}

bool St7565Display::command(const std::uint8_t value)
{
    gpio_set_level(static_cast<gpio_num_t>(config_.data_command_gpio), 0);
    return data(&value, 1);
}

bool St7565Display::data(const std::uint8_t* bytes, const std::size_t count)
{
    if (!device_) return false;
    spi_transaction_t transaction{};
    transaction.length = count * 8;
    transaction.tx_buffer = bytes;
    const bool ok = spi_device_polling_transmit(static_cast<spi_device_handle_t>(device_),
                                                &transaction) == ESP_OK;
    gpio_set_level(static_cast<gpio_num_t>(config_.data_command_gpio), 1);
    return ok;
}

}  // namespace bikeos
