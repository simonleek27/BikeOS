#pragma once

#include <cstddef>
#include <cstdint>

#include <vector>

#include "bikeos/canvas.hpp"
#include "bikeos/framebuffer.hpp"

namespace bikeos {

struct Ili9341Config {
    int spi_host{1};
    int mosi_gpio{-1};
    int clock_gpio{-1};
    int chip_select_gpio{-1};
    int data_command_gpio{-1};
    int reset_gpio{-1};
    int clock_hz{26'000'000};
};

// 2.8-inch 320x240 ILI9341 TFT over 4-wire SPI. MISO is intentionally unused
// for the first write-only display test.
class Ili9341Display {
public:
    explicit Ili9341Display(Ili9341Config config);
    ~Ili9341Display();

    Ili9341Display(const Ili9341Display&) = delete;
    Ili9341Display& operator=(const Ili9341Display&) = delete;

    bool initialize();
    bool flush(const Framebuffer& framebuffer);
    // Sends a full-color 320x240 frame. Only 8-row bands that changed since
    // the previous call are transferred; returns the number of rows sent via
    // `rows_sent` when given.
    bool flush(const Canvas565& canvas, int* rows_sent = nullptr);
    bool show_test_pattern();

private:
    bool command(std::uint8_t value);
    bool data(const std::uint8_t* bytes, std::size_t count);
    bool set_window(std::uint16_t left, std::uint16_t top,
                    std::uint16_t right, std::uint16_t bottom);
    bool write_row(const std::uint8_t* bytes, std::size_t count);

    Ili9341Config config_;
    std::uint8_t* dma_buffer_{};  // kChunkRows rows of big-endian RGB565
    std::vector<std::uint16_t> previous_frame_;
    bool have_previous_frame_{};
    void* device_{};
    bool bus_initialized_{};
    bool initialized_{};
};

}  // namespace bikeos
