#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace bikeos {

struct Font;

struct PointF {
    float x{};
    float y{};
};

constexpr std::uint16_t rgb565(const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue)
{
    return static_cast<std::uint16_t>(((red & 0xF8U) << 8U) | ((green & 0xFCU) << 3U) | (blue >> 3U));
}

// Blend `source` over `destination`; alpha 0 keeps destination, 255 is source.
std::uint16_t blend565(std::uint16_t destination, std::uint16_t source, std::uint8_t alpha);

// Full-color RGB565 drawing surface for the 320x240 TFT. Pixels are stored in
// host byte order; the display driver swaps to the panel's big-endian order.
class Canvas565 {
public:
    static constexpr int default_width = 320;
    static constexpr int default_height = 240;

    explicit Canvas565(int width = default_width, int height = default_height);

    int width() const { return width_; }
    int height() const { return height_; }
    std::span<const std::uint16_t> pixels() const { return pixels_; }
    std::span<const std::uint16_t> row(int y) const;

    void clear(std::uint16_t color);
    std::uint16_t pixel(int x, int y) const;
    void set_pixel(int x, int y, std::uint16_t color);
    void blend_pixel(int x, int y, std::uint16_t color, std::uint8_t alpha);

    void fill_rect(int x, int y, int rect_width, int rect_height, std::uint16_t color);
    // Anti-aliased rounded rectangle; `radius` is clamped to half the short side.
    void fill_round_rect(int x, int y, int rect_width, int rect_height, int radius, std::uint16_t color);
    void stroke_round_rect(int x, int y, int rect_width, int rect_height, int radius, std::uint16_t color);
    void fill_circle(PointF centre, float radius, std::uint16_t color);
    // Anti-aliased line with round caps. Overlapping segments of one polyline
    // share caps, so polylines get round joins.
    void thick_line(PointF a, PointF b, float width, std::uint16_t color);
    void polyline(std::span<const PointF> points, float width, std::uint16_t color);
    // Even-odd scanline fill across all rings, so inner rings punch holes.
    void fill_polygon(std::span<const std::span<const PointF>> rings, std::uint16_t color);
    void fill_polygon(std::span<const PointF> ring, std::uint16_t color);

    // Draws UTF-8 text with its baseline at `baseline_y`; returns the advance.
    int draw_text(const Font& font, int x, int baseline_y, std::string_view utf8, std::uint16_t color);
    // Same, with a 1 px outline in `halo` for labels over busy map areas.
    int draw_text_halo(const Font& font, int x, int baseline_y, std::string_view utf8,
                       std::uint16_t color, std::uint16_t halo);

private:
    int width_;
    int height_;
    std::vector<std::uint16_t> pixels_;
};

}  // namespace bikeos
