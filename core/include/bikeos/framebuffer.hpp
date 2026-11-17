#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace bikeos {

class Framebuffer {
public:
    static constexpr int width = 128;
    static constexpr int height = 64;
    static constexpr int page_count = height / 8;
    static constexpr std::size_t byte_count = width * page_count;

    void clear(bool on = false);
    bool pixel(int x, int y) const;
    void set_pixel(int x, int y, bool on = true);
    void fill_rect(int x, int y, int rect_width, int rect_height, bool on = true);
    void line(int x0, int y0, int x1, int y1, bool on = true);
    void thick_line(int x0, int y0, int x1, int y1, int thickness, bool on = true);
    void text_3x5(int x, int y, std::string_view text, bool on = true, int spacing = 1);
    void text_5x7(int x, int y, std::string_view text, bool on = true, int spacing = 1);
    static int text_5x7_width(std::string_view text, int spacing = 1);

    const std::array<std::uint8_t, byte_count>& bytes() const;

private:
    std::array<std::uint8_t, byte_count> data_{};
};

}  // namespace bikeos
