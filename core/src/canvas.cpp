#include "bikeos/canvas.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "bikeos/fonts.hpp"

namespace bikeos {
namespace {

std::uint8_t coverage_alpha(const float coverage)
{
    if (coverage <= 0.0F) return 0;
    if (coverage >= 1.0F) return 255;
    return static_cast<std::uint8_t>(coverage * 255.0F + 0.5F);
}

}  // namespace

std::uint16_t blend565(const std::uint16_t destination, const std::uint16_t source, const std::uint8_t alpha)
{
    if (alpha == 0) return destination;
    if (alpha == 255) return source;
    // Spread 5-6-5 into 0x07E0F81F lanes so one multiply blends all channels
    // with 5-bit alpha; no divisions in the per-pixel path.
    const std::uint32_t a = (static_cast<std::uint32_t>(alpha) + 4U) >> 3U;
    const std::uint32_t foreground = (source | (static_cast<std::uint32_t>(source) << 16U)) & 0x07E0F81FU;
    const std::uint32_t background = (destination | (static_cast<std::uint32_t>(destination) << 16U)) & 0x07E0F81FU;
    const std::uint32_t mixed = ((foreground * a + background * (32U - a)) >> 5U) & 0x07E0F81FU;
    return static_cast<std::uint16_t>(mixed | (mixed >> 16U));
}

Canvas565::Canvas565(const int width, const int height)
    : width_(std::max(1, width)),
      height_(std::max(1, height)),
      pixels_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_))
{
}

std::span<const std::uint16_t> Canvas565::row(const int y) const
{
    if (y < 0 || y >= height_) return {};
    return std::span<const std::uint16_t>(pixels_).subspan(static_cast<std::size_t>(y) * width_, width_);
}

void Canvas565::clear(const std::uint16_t color)
{
    std::fill(pixels_.begin(), pixels_.end(), color);
}

std::uint16_t Canvas565::pixel(const int x, const int y) const
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return 0;
    return pixels_[static_cast<std::size_t>(y) * width_ + x];
}

void Canvas565::set_pixel(const int x, const int y, const std::uint16_t color)
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    pixels_[static_cast<std::size_t>(y) * width_ + x] = color;
}

void Canvas565::blend_pixel(const int x, const int y, const std::uint16_t color, const std::uint8_t alpha)
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || alpha == 0) return;
    auto& target = pixels_[static_cast<std::size_t>(y) * width_ + x];
    target = blend565(target, color, alpha);
}

void Canvas565::fill_rect(const int x, const int y, const int rect_width, const int rect_height,
                          const std::uint16_t color)
{
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(width_, x + rect_width);
    const int bottom = std::min(height_, y + rect_height);
    for (int row_y = top; row_y < bottom; ++row_y) {
        auto* start = pixels_.data() + static_cast<std::size_t>(row_y) * width_;
        std::fill(start + left, start + std::max(left, right), color);
    }
}

void Canvas565::fill_round_rect(const int x, const int y, const int rect_width, const int rect_height,
                                const int radius, const std::uint16_t color)
{
    if (rect_width <= 0 || rect_height <= 0) return;
    const int r = std::clamp(radius, 0, std::min(rect_width, rect_height) / 2);
    if (r == 0) {
        fill_rect(x, y, rect_width, rect_height, color);
        return;
    }
    fill_rect(x, y + r, rect_width, rect_height - 2 * r, color);
    fill_rect(x + r, y, rect_width - 2 * r, r, color);
    fill_rect(x + r, y + rect_height - r, rect_width - 2 * r, r, color);
    // Corner quadrants with coverage from the distance to each corner centre.
    const std::array<PointF, 4> centres{{
        {static_cast<float>(x + r), static_cast<float>(y + r)},
        {static_cast<float>(x + rect_width - r), static_cast<float>(y + r)},
        {static_cast<float>(x + r), static_cast<float>(y + rect_height - r)},
        {static_cast<float>(x + rect_width - r), static_cast<float>(y + rect_height - r)},
    }};
    const std::array<std::array<int, 2>, 4> origins{{
        {x, y}, {x + rect_width - r, y}, {x, y + rect_height - r}, {x + rect_width - r, y + rect_height - r},
    }};
    for (std::size_t corner = 0; corner < 4; ++corner) {
        for (int dy = 0; dy < r; ++dy) {
            for (int dx = 0; dx < r; ++dx) {
                const int px = origins[corner][0] + dx;
                const int py = origins[corner][1] + dy;
                const float distance = std::hypot(px + 0.5F - centres[corner].x, py + 0.5F - centres[corner].y);
                blend_pixel(px, py, color, coverage_alpha(static_cast<float>(r) - distance + 0.5F));
            }
        }
    }
}

void Canvas565::stroke_round_rect(const int x, const int y, const int rect_width, const int rect_height,
                                  const int radius, const std::uint16_t color)
{
    if (rect_width <= 1 || rect_height <= 1) return;
    const int r = std::clamp(radius, 0, std::min(rect_width, rect_height) / 2);
    fill_rect(x + r, y, rect_width - 2 * r, 1, color);
    fill_rect(x + r, y + rect_height - 1, rect_width - 2 * r, 1, color);
    fill_rect(x, y + r, 1, rect_height - 2 * r, color);
    fill_rect(x + rect_width - 1, y + r, 1, rect_height - 2 * r, color);
    if (r == 0) return;
    const std::array<PointF, 4> centres{{
        {static_cast<float>(x + r), static_cast<float>(y + r)},
        {static_cast<float>(x + rect_width - r), static_cast<float>(y + r)},
        {static_cast<float>(x + r), static_cast<float>(y + rect_height - r)},
        {static_cast<float>(x + rect_width - r), static_cast<float>(y + rect_height - r)},
    }};
    const std::array<std::array<int, 2>, 4> origins{{
        {x, y}, {x + rect_width - r, y}, {x, y + rect_height - r}, {x + rect_width - r, y + rect_height - r},
    }};
    for (std::size_t corner = 0; corner < 4; ++corner) {
        for (int dy = 0; dy < r; ++dy) {
            for (int dx = 0; dx < r; ++dx) {
                const int px = origins[corner][0] + dx;
                const int py = origins[corner][1] + dy;
                const float distance = std::hypot(px + 0.5F - centres[corner].x, py + 0.5F - centres[corner].y);
                // 1 px ring centred half a pixel inside the edge.
                const float coverage = 1.0F - std::abs(distance - (static_cast<float>(r) - 0.5F));
                blend_pixel(px, py, color, coverage_alpha(coverage));
            }
        }
    }
}

void Canvas565::fill_circle(const PointF centre, const float radius, const std::uint16_t color)
{
    if (radius <= 0.0F) return;
    const int left = static_cast<int>(std::floor(centre.x - radius - 1.0F));
    const int right = static_cast<int>(std::ceil(centre.x + radius + 1.0F));
    const int top = static_cast<int>(std::floor(centre.y - radius - 1.0F));
    const int bottom = static_cast<int>(std::ceil(centre.y + radius + 1.0F));
    const float inner = std::max(0.0F, radius - 0.5F);
    const float inner_sq = inner * inner;
    const float outer_sq = (radius + 0.5F) * (radius + 0.5F);
    for (int y = std::max(0, top); y <= std::min(height_ - 1, bottom); ++y) {
        for (int x = std::max(0, left); x <= std::min(width_ - 1, right); ++x) {
            const float dx = x + 0.5F - centre.x;
            const float dy = y + 0.5F - centre.y;
            const float squared = dx * dx + dy * dy;
            if (squared <= inner_sq) {
                pixels_[static_cast<std::size_t>(y) * width_ + x] = color;
            } else if (squared < outer_sq) {
                blend_pixel(x, y, color, coverage_alpha(radius - std::sqrt(squared) + 0.5F));
            }
        }
    }
}

void Canvas565::thick_line(const PointF a, const PointF b, const float width, const std::uint16_t color)
{
    const float half = std::max(0.5F, width * 0.5F);
    const float reach = half + 1.0F;
    const int top = std::max(0, static_cast<int>(std::floor(std::min(a.y, b.y) - reach)));
    const int bottom = std::min(height_ - 1, static_cast<int>(std::ceil(std::max(a.y, b.y) + reach)));
    const int box_left = std::max(0, static_cast<int>(std::floor(std::min(a.x, b.x) - reach)));
    const int box_right = std::min(width_ - 1, static_cast<int>(std::ceil(std::max(a.x, b.x) + reach)));
    if (top > bottom || box_left > box_right) return;
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float length_sq = dx * dx + dy * dy;
    const float length = std::sqrt(length_sq);
    const float inverse_length_sq = length_sq > 0.0F ? 1.0F / length_sq : 0.0F;
    const float inner = std::max(0.0F, half - 0.5F);
    const float inner_sq = inner * inner;
    const float outer_sq = (half + 0.5F) * (half + 0.5F);
    for (int y = top; y <= bottom; ++y) {
        const float py = y + 0.5F;
        int left = box_left;
        int right = box_right;
        // Narrow each row to the strip around the infinite line so long
        // diagonal segments do not visit their whole bounding box.
        if (length > 0.0F && std::abs(dy) > 1.0e-3F) {
            const float x_at_row = a.x + dx * (py - a.y) / dy;
            const float spread = reach * length / std::abs(dy);
            left = std::max(left, static_cast<int>(std::floor(x_at_row - spread)));
            right = std::min(right, static_cast<int>(std::ceil(x_at_row + spread)));
        }
        auto* row_pixels = pixels_.data() + static_cast<std::size_t>(y) * width_;
        for (int x = left; x <= right; ++x) {
            const float px = x + 0.5F;
            float t = 0.0F;
            if (length_sq > 0.0F) t = std::clamp(((px - a.x) * dx + (py - a.y) * dy) * inverse_length_sq, 0.0F, 1.0F);
            const float ex = px - (a.x + dx * t);
            const float ey = py - (a.y + dy * t);
            const float squared = ex * ex + ey * ey;
            // Most pixels are fully in or out; only the 1 px rim needs a sqrt.
            if (squared <= inner_sq) {
                row_pixels[x] = color;
            } else if (squared < outer_sq) {
                row_pixels[x] = blend565(row_pixels[x], color, coverage_alpha(half - std::sqrt(squared) + 0.5F));
            }
        }
    }
}

void Canvas565::polyline(const std::span<const PointF> points, const float width, const std::uint16_t color)
{
    if (points.size() == 1) fill_circle(points.front(), width * 0.5F, color);
    for (std::size_t i = 1; i < points.size(); ++i) thick_line(points[i - 1], points[i], width, color);
}

void Canvas565::fill_polygon(const std::span<const std::span<const PointF>> rings, const std::uint16_t color)
{
    float min_y = static_cast<float>(height_);
    float max_y = 0.0F;
    for (const auto ring : rings) {
        for (const auto point : ring) {
            min_y = std::min(min_y, point.y);
            max_y = std::max(max_y, point.y);
        }
    }
    const int top = std::max(0, static_cast<int>(std::floor(min_y)));
    const int bottom = std::min(height_ - 1, static_cast<int>(std::ceil(max_y)));
    std::vector<float> crossings;
    for (int y = top; y <= bottom; ++y) {
        const float py = y + 0.5F;
        crossings.clear();
        for (const auto ring : rings) {
            const std::size_t count = ring.size();
            if (count < 3) continue;
            for (std::size_t i = 0; i < count; ++i) {
                const PointF p = ring[i];
                const PointF q = ring[(i + 1) % count];
                if ((p.y <= py && q.y > py) || (q.y <= py && p.y > py)) {
                    crossings.push_back(p.x + (py - p.y) * (q.x - p.x) / (q.y - p.y));
                }
            }
        }
        std::sort(crossings.begin(), crossings.end());
        auto* row_start = pixels_.data() + static_cast<std::size_t>(y) * width_;
        for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
            const int left = std::max(0, static_cast<int>(std::lround(crossings[i])));
            const int right = std::min(width_, static_cast<int>(std::lround(crossings[i + 1])));
            if (left < right) std::fill(row_start + left, row_start + right, color);
        }
    }
}

void Canvas565::fill_polygon(const std::span<const PointF> ring, const std::uint16_t color)
{
    const std::array<std::span<const PointF>, 1> rings{ring};
    fill_polygon(rings, color);
}

int Canvas565::draw_text(const Font& font, const int x, const int baseline_y, const std::string_view utf8,
                         const std::uint16_t color)
{
    int pen = x;
    std::size_t index = 0;
    while (index < utf8.size()) {
        const Glyph* glyph = font.find(next_codepoint(utf8, index));
        if (!glyph) glyph = font.find('?');
        if (!glyph) continue;
        const int stride = (glyph->width + 1) / 2;
        const std::uint8_t* bitmap = font.bitmap + glyph->bitmap_offset;
        for (int gy = 0; gy < glyph->height; ++gy) {
            for (int gx = 0; gx < glyph->width; ++gx) {
                const std::uint8_t packed = bitmap[gy * stride + gx / 2];
                const std::uint8_t level = (gx % 2 == 0) ? (packed >> 4U) : (packed & 0x0FU);
                if (level == 0) continue;
                blend_pixel(pen + glyph->x_offset + gx, baseline_y + glyph->y_offset + gy, color,
                            static_cast<std::uint8_t>(level * 17U));
            }
        }
        pen += glyph->advance;
    }
    return pen - x;
}

int Canvas565::draw_text_halo(const Font& font, const int x, const int baseline_y, const std::string_view utf8,
                              const std::uint16_t color, const std::uint16_t halo)
{
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx != 0 || dy != 0) draw_text(font, x + dx, baseline_y + dy, utf8, halo);
        }
    }
    return draw_text(font, x, baseline_y, utf8, color);
}

}  // namespace bikeos
