#include "uart_gps_source.hpp"

#include <algorithm>
#include <array>
#include <cinttypes>
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
    // Drain everything buffered since the last call. A NEO-6M sends ~450 B/s
    // and the caller steps about once a second, so a single fixed-size read
    // falls behind, fills the driver buffer, and drops sentences.
    std::array<std::uint8_t, 256> input{};
    std::optional<GpsFix> latest;
    for (;;) {
        const int count = uart_read_bytes(static_cast<uart_port_t>(config_.uart_number),
                                          input.data(), input.size(), 0);
        if (count <= 0) break;
        bytes_received_ += static_cast<std::uint32_t>(count);
        for (int index = 0; index < count; ++index) {
            consume(static_cast<char>(input[static_cast<std::size_t>(index)]),
                    monotonic_timestamp_ms, latest);
        }
        if (count < static_cast<int>(input.size())) break;
    }

    if (monotonic_timestamp_ms - last_stats_log_ms_ >= 5000) {
        last_stats_log_ms_ = monotonic_timestamp_ms;
        ESP_LOGI(kTag, "rx bytes=%" PRIu32 " lines=%" PRIu32 " parsed=%" PRIu32
                       " valid=%" PRIu32 " overflow=%" PRIu32,
                 bytes_received_, lines_received_, sentences_parsed_, valid_fixes_, overflow_count_);
        if (last_rmc_length_ > 0) {
            ESP_LOGI(kTag, "last RMC: %.*s", static_cast<int>(last_rmc_length_), last_rmc_.data());
        }
        if (last_gsv_length_ > 0) {
            ESP_LOGI(kTag, "last GSV: %.*s", static_cast<int>(last_gsv_length_), last_gsv_.data());
        }
        if (bytes_received_ == 0) {
            ESP_LOGW(kTag, "No bytes on GPIO%d: check GPS TX -> ESP RX wire, VCC and GND",
                     config_.rx_gpio);
        }
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
            ++lines_received_;
            const std::string_view sentence(line_.data(), line_length_);
            if (sentence.size() > 6 && sentence.substr(3, 3) == "RMC") {
                last_rmc_length_ = std::min(sentence.size(), last_rmc_.size());
                std::copy_n(sentence.data(), last_rmc_length_, last_rmc_.data());
            }
            if (sentence.size() > 6 && sentence.substr(3, 3) == "GSV") {
                last_gsv_length_ = std::min(sentence.size(), last_gsv_.size());
                std::copy_n(sentence.data(), last_gsv_length_, last_gsv_.data());
            }
            if (auto parsed = parser_.parse(sentence, timestamp_ms)) {
                ++sentences_parsed_;
                if (parsed->position_valid) ++valid_fixes_;
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
