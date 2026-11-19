#include "ili9341_display.hpp"

#include <array>
#include <algorithm>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace bikeos {
namespace {

constexpr char kTag[] = "ili9341";
constexpr std::uint16_t kWidth = 320;
constexpr std::uint16_t kHeight = 240;
constexpr std::size_t kRowBytes = static_cast<std::size_t>(kWidth) * 2U;
constexpr int kChunkRows = 8;  // rows per SPI transfer, and the dirty-band height

void put_be16(std::uint8_t* destination, const std::uint16_t value)
{
    destination[0] = static_cast<std::uint8_t>(value >> 8U);
    destination[1] = static_cast<std::uint8_t>(value & 0xFFU);
}
}  // namespace

Ili9341Display::Ili9341Display(Ili9341Config config) : config_(config) {}

Ili9341Display::~Ili9341Display()
{
    if (device_) spi_bus_remove_device(static_cast<spi_device_handle_t>(device_));
    if (dma_buffer_) heap_caps_free(dma_buffer_);
    if (bus_initialized_) spi_bus_free(static_cast<spi_host_device_t>(config_.spi_host));
}

bool Ili9341Display::initialize()
{
    if (initialized_) return true;
    if (config_.mosi_gpio < 0 || config_.clock_gpio < 0 || config_.chip_select_gpio < 0 ||
        config_.data_command_gpio < 0 || config_.reset_gpio < 0 || config_.clock_hz <= 0) {
        ESP_LOGE(kTag, "Refusing to use incomplete ILI9341 GPIO configuration");
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
        .max_transfer_sz = static_cast<int>(kRowBytes * kChunkRows),
        .flags = 0,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = 0,
    };
    dma_buffer_ = static_cast<std::uint8_t*>(heap_caps_malloc(kRowBytes * kChunkRows, MALLOC_CAP_DMA));
    if (!dma_buffer_) {
        ESP_LOGE(kTag, "No DMA memory for the TFT transfer buffer");
        return false;
    }
    const auto host = static_cast<spi_host_device_t>(config_.spi_host);
    if (spi_bus_initialize(host, &bus_config, SPI_DMA_CH_AUTO) != ESP_OK) return false;
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
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(static_cast<gpio_num_t>(config_.reset_gpio), 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    if (!command(0x01)) return false;  // software reset
    vTaskDelay(pdMS_TO_TICKS(150));
    if (!command(0x11)) return false;  // sleep out
    vTaskDelay(pdMS_TO_TICKS(120));

    // Landscape: MX + BGR. The board's backlight is powered separately.
    if (!command(0x36)) return false;
    const std::uint8_t madctl = 0x28;
    if (!data(&madctl, 1)) return false;
    if (!command(0x3A)) return false;
    const std::uint8_t pixel_format = 0x55;  // RGB565
    if (!data(&pixel_format, 1)) return false;

    const std::array<std::uint8_t, 2> frame_rate{{0x00, 0x1B}};
    if (!command(0xB1) || !data(frame_rate.data(), frame_rate.size())) return false;
    const std::array<std::uint8_t, 3> display_function{{0x08, 0x82, 0x27}};
    if (!command(0xB6) || !data(display_function.data(), display_function.size())) return false;
    const std::uint8_t power_control_1 = 0x23;
    if (!command(0xC0) || !data(&power_control_1, 1)) return false;
    const std::uint8_t power_control_2 = 0x10;
    if (!command(0xC1) || !data(&power_control_2, 1)) return false;
    const std::array<std::uint8_t, 2> vcom_1{{0x3E, 0x28}};
    if (!command(0xC5) || !data(vcom_1.data(), vcom_1.size())) return false;
    const std::uint8_t vcom_2 = 0x86;
    if (!command(0xC7) || !data(&vcom_2, 1)) return false;

    const std::array<std::uint8_t, 15> gamma_positive{{
        0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
        0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
    }};
    const std::array<std::uint8_t, 15> gamma_negative{{
        0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
        0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
    }};
    if (!command(0xE0) || !data(gamma_positive.data(), gamma_positive.size())) return false;
    if (!command(0xE1) || !data(gamma_negative.data(), gamma_negative.size())) return false;
    if (!command(0x29)) return false;  // display on
    vTaskDelay(pdMS_TO_TICKS(20));

    initialized_ = true;
    ESP_LOGI(kTag, "ILI9341 initialized at %d Hz, landscape 320x240", config_.clock_hz);
    return true;
}

bool Ili9341Display::command(const std::uint8_t value)
{
    gpio_set_level(static_cast<gpio_num_t>(config_.data_command_gpio), 0);
    return data(&value, 1);
}

bool Ili9341Display::data(const std::uint8_t* bytes, const std::size_t count)
{
    if (!device_ || !bytes || count == 0) return false;
    spi_transaction_t transaction{};
    transaction.length = count * 8U;
    transaction.tx_buffer = bytes;
    const bool ok = spi_device_polling_transmit(static_cast<spi_device_handle_t>(device_), &transaction) == ESP_OK;
    gpio_set_level(static_cast<gpio_num_t>(config_.data_command_gpio), 1);
    return ok;
}

bool Ili9341Display::set_window(const std::uint16_t left, const std::uint16_t top,
                                const std::uint16_t right, const std::uint16_t bottom)
{
    std::array<std::uint8_t, 4> coordinates{};
    if (!command(0x2A)) return false;
    put_be16(coordinates.data(), left);
    put_be16(coordinates.data() + 2, right);
    if (!data(coordinates.data(), coordinates.size())) return false;
    if (!command(0x2B)) return false;
    put_be16(coordinates.data(), top);
    put_be16(coordinates.data() + 2, bottom);
    if (!data(coordinates.data(), coordinates.size())) return false;
    return command(0x2C);
}

bool Ili9341Display::write_row(const std::uint8_t* bytes, const std::size_t count)
{
    if (!bytes || count != kRowBytes) return false;
    return data(bytes, count);
}

bool Ili9341Display::flush(const Framebuffer& framebuffer)
{
    if (!initialized_ || !set_window(0, 0, kWidth - 1, kHeight - 1)) return false;
    std::array<std::uint8_t, kRowBytes> row{};
    for (std::uint16_t y = 0; y < kHeight; ++y) {
        const int source_y = static_cast<int>((static_cast<std::uint32_t>(y) * Framebuffer::height) / kHeight);
        for (std::uint16_t x = 0; x < kWidth; ++x) {
            const int source_x = static_cast<int>((static_cast<std::uint32_t>(x) * Framebuffer::width) / kWidth);
            const std::uint16_t color = framebuffer.pixel(source_x, source_y) ? 0xFFFF : 0x0000;
            put_be16(row.data() + static_cast<std::size_t>(x) * 2U, color);
        }
        if (!write_row(row.data(), row.size())) return false;
    }
    return true;
}

bool Ili9341Display::flush(const Canvas565& canvas, int* rows_sent)
{
    if (rows_sent) *rows_sent = 0;
    if (!initialized_ || canvas.width() != kWidth || canvas.height() != kHeight) return false;
    const auto pixels = canvas.pixels();
    if (previous_frame_.size() != pixels.size()) {
        previous_frame_.assign(pixels.size(), 0);
        have_previous_frame_ = false;
    }
    int band = 0;
    while (band * kChunkRows < kHeight) {
        const auto changed = [&](const int index) {
            const std::size_t start = static_cast<std::size_t>(index) * kChunkRows * kWidth;
            const std::size_t count = std::min<std::size_t>(kChunkRows * kWidth, pixels.size() - start);
            return !have_previous_frame_ ||
                   !std::equal(pixels.begin() + start, pixels.begin() + start + count, previous_frame_.begin() + start);
        };
        if (!changed(band)) {
            ++band;
            continue;
        }
        // Merge consecutive changed bands into one window.
        int end = band + 1;
        while (end * kChunkRows < kHeight && changed(end)) ++end;
        const int top = band * kChunkRows;
        const int bottom = std::min<int>(kHeight, end * kChunkRows);
        if (!set_window(0, static_cast<std::uint16_t>(top), kWidth - 1, static_cast<std::uint16_t>(bottom - 1))) {
            return false;
        }
        for (int y = top; y < bottom; y += kChunkRows) {
            const int rows = std::min(kChunkRows, bottom - y);
            for (int row = 0; row < rows; ++row) {
                const auto source = canvas.row(y + row);
                std::uint8_t* destination = dma_buffer_ + static_cast<std::size_t>(row) * kRowBytes;
                for (int x = 0; x < kWidth; ++x) put_be16(destination + x * 2, source[static_cast<std::size_t>(x)]);
            }
            if (!data(dma_buffer_, static_cast<std::size_t>(rows) * kRowBytes)) return false;
        }
        std::copy(pixels.begin() + static_cast<std::ptrdiff_t>(top) * kWidth,
                  pixels.begin() + static_cast<std::ptrdiff_t>(bottom) * kWidth,
                  previous_frame_.begin() + static_cast<std::ptrdiff_t>(top) * kWidth);
        if (rows_sent) *rows_sent += bottom - top;
        band = end;
    }
    have_previous_frame_ = true;
    return true;
}

bool Ili9341Display::show_test_pattern()
{
    if (!initialized_ || !set_window(0, 0, kWidth - 1, kHeight - 1)) return false;
    std::array<std::uint8_t, kRowBytes> row{};
    for (std::uint16_t y = 0; y < kHeight; ++y) {
        for (std::uint16_t x = 0; x < kWidth; ++x) {
            std::uint16_t color{};
            if (x < 4 || x >= kWidth - 4 || y < 4 || y >= kHeight - 4) {
                color = 0xFFFF;  // white border
            } else if (x >= 156 && x < 164) {
                color = 0x0000;  // vertical black crosshair
            } else if (y >= 116 && y < 124) {
                color = 0x0000;  // horizontal black crosshair
            } else {
                const std::uint16_t band = static_cast<std::uint16_t>(y / 40U);
                switch (band % 6U) {
                case 0: color = 0xF800; break;  // red
                case 1: color = 0x07E0; break;  // green
                case 2: color = 0x001F; break;  // blue
                case 3: color = 0xFFE0; break;  // yellow
                case 4: color = 0x07FF; break;  // cyan
                default: color = 0xF81F; break; // magenta
                }
            }
            put_be16(row.data() + static_cast<std::size_t>(x) * 2U, color);
        }
        if (!write_row(row.data(), row.size())) return false;
    }
    ESP_LOGI(kTag, "ILI9341 color-bar test pattern sent");
    return true;
}

}  // namespace bikeos
