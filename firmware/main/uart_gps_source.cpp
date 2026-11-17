#include "uart_gps_source.hpp"

#include <array>
#include <string_view>

#include "driver/uart.h"
#include "esp_log.h"

namespace bikeos {
namespace {
constexpr char kTag[] = "uart_gps";
}

UartGpsSource::UartGpsSource(UartGpsConfig config) : config_(config) {}

UartGpsSource::~UartGpsSource()
{
    if (initialized_) uart_driver_delete(static_cast<uart_port_t>(config_.uart_number));
}

bool UartGpsSource::initialize()
{
    if (initialized_) return true;
    if (config_.uart_number < 0 || config_.uart_number >= UART_NUM_MAX ||
        config_.rx_gpio < 0 || config_.baud_rate <= 0) {
        ESP_LOGE(kTag, "Refusing unverified UART configuration (uart=%d rx=%d baud=%d)",
                 config_.uart_number, config_.rx_gpio, config_.baud_rate);
        return false;
    }

    const uart_config_t uart_config{
        .baud_rate = config_.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };
    const auto port = static_cast<uart_port_t>(config_.uart_number);
    if (uart_param_config(port, &uart_config) != ESP_OK ||
        uart_set_pin(port, config_.tx_gpio, config_.rx_gpio,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK ||
        uart_driver_install(port, 2048, 0, 0, nullptr, 0) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to initialize GPS UART%d", config_.uart_number);
        return false;
    }
    initialized_ = true;
    ESP_LOGI(kTag, "Listening for NMEA on UART%d RX GPIO%d at %d baud",
             config_.uart_number, config_.rx_gpio, config_.baud_rate);
    return true;
}

std::optional<GpsFix> UartGpsSource::poll(const std::uint64_t monotonic_timestamp_ms)
{
    if (!initialized_) return std::nullopt;
    std::array<std::uint8_t, 256> input{};
    const int count = uart_read_bytes(static_cast<uart_port_t>(config_.uart_number),
                                      input.data(), input.size(), 0);
    std::optional<GpsFix> latest;
    for (int index = 0; index < count; ++index) {
        consume(static_cast<char>(input[static_cast<std::size_t>(index)]),
                monotonic_timestamp_ms, latest);
    }
    return latest;
}

void UartGpsSource::consume(const char byte,
                            const std::uint64_t timestamp_ms,
                            std::optional<GpsFix>& latest)
{
    if (byte == '\r') return;
    if (byte == '\n') {
        if (line_length_ > 0) {
            if (auto parsed = parser_.parse(std::string_view(line_.data(), line_length_), timestamp_ms)) {
                latest = *parsed;
            }
        }
        line_length_ = 0;
        return;
    }
    if (line_length_ == line_.size()) {
        ++overflow_count_;
        line_length_ = 0;
        return;
    }
    line_[line_length_++] = byte;
}

}  // namespace bikeos
