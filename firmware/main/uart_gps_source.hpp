#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "bikeos/gps.hpp"
#include "bikeos/nmea.hpp"

namespace bikeos {

struct UartGpsConfig {
    int uart_number{1};
    int rx_gpio{-1};
    int tx_gpio{-1};
    int baud_rate{9600};
};

// Non-blocking NMEA source for a TTL GPS receiver. The caller owns the UART
// pin selection: no unverified prototype GPIO is compiled into this driver.
class UartGpsSource {
public:
    explicit UartGpsSource(UartGpsConfig config);
    ~UartGpsSource();

    UartGpsSource(const UartGpsSource&) = delete;
    UartGpsSource& operator=(const UartGpsSource&) = delete;

    bool initialize();
    std::optional<GpsFix> poll(std::uint64_t monotonic_timestamp_ms);
    std::uint32_t overflow_count() const { return overflow_count_; }

private:
    void consume(char byte, std::uint64_t timestamp_ms, std::optional<GpsFix>& latest);

    UartGpsConfig config_;
    NmeaParser parser_;
    std::array<char, 128> line_{};
    std::size_t line_length_{};
    std::uint32_t overflow_count_{};
    std::uint32_t bytes_received_{};
    std::uint32_t lines_received_{};
    std::uint32_t sentences_parsed_{};
    std::uint32_t valid_fixes_{};
    std::uint64_t last_stats_log_ms_{};
    std::array<char, 96> last_rmc_{};
    std::size_t last_rmc_length_{};
    std::array<char, 96> last_gsv_{};
    std::size_t last_gsv_length_{};
    bool initialized_{};
};

}  // namespace bikeos
