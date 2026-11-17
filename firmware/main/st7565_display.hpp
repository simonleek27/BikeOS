#pragma once

#include <cstddef>
#include <cstdint>

#include "bikeos/framebuffer.hpp"

namespace bikeos {

struct St7565Config {
    int spi_host{1};
    int mosi_gpio{-1};
    int clock_gpio{-1};
    int chip_select_gpio{-1};
    int data_command_gpio{-1};
    int reset_gpio{-1};
    int clock_hz{1'000'000};
    std::uint8_t column_offset{};
    std::uint8_t contrast{0x18};
    bool reverse_com{true};
    bool reverse_adc{false};
};

// SPI transport for the verified ST7565R controller. The panel-specific
// orientation, contrast and column offset remain explicit configuration.
class St7565Display {
public:
    explicit St7565Display(St7565Config config);
    ~St7565Display();

    St7565Display(const St7565Display&) = delete;
    St7565Display& operator=(const St7565Display&) = delete;

    bool initialize();
    bool flush(const Framebuffer& framebuffer);

private:
    bool command(std::uint8_t value);
    bool data(const std::uint8_t* bytes, std::size_t count);

    St7565Config config_;
    void* device_{};
    bool bus_initialized_{};
    bool initialized_{};
};

}  // namespace bikeos
