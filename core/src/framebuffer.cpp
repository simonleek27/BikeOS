#include "bikeos/framebuffer.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace bikeos {
namespace {

struct Glyph {
    char character;
    std::array<std::uint8_t, 3> columns;
};

struct Glyph5x7 {
    char character;
    std::array<std::uint8_t, 5> columns;
};

constexpr std::array<Glyph, 44> kGlyphs{{
    {' ', {0, 0, 0}}, {'-', {4, 4, 4}}, {'.', {0, 0, 16}}, {'/', {24, 4, 3}},
    {'0', {31, 17, 31}}, {'1', {0, 31, 0}}, {'2', {29, 21, 23}}, {'3', {21, 21, 31}},
    {'4', {7, 4, 31}}, {'5', {23, 21, 29}}, {'6', {31, 21, 29}}, {'7', {1, 1, 31}},
    {'8', {31, 21, 31}}, {'9', {23, 21, 31}}, {'A', {30, 5, 30}}, {'B', {31, 21, 10}},
    {'C', {14, 17, 17}}, {'D', {31, 17, 14}}, {'E', {31, 21, 17}}, {'F', {31, 5, 1}},
    {'G', {14, 17, 29}}, {'H', {31, 4, 31}}, {'I', {17, 31, 17}}, {'J', {8, 16, 15}},
    {'K', {31, 4, 27}}, {'L', {31, 16, 16}}, {'M', {31, 2, 31}}, {'N', {31, 1, 30}},
    {'O', {14, 17, 14}}, {'P', {31, 5, 2}}, {'Q', {14, 25, 30}}, {'R', {31, 5, 26}},
    {'S', {18, 21, 9}}, {'T', {1, 31, 1}}, {'U', {15, 16, 15}}, {'V', {7, 24, 7}},
    {'W', {31, 8, 31}}, {'X', {27, 4, 27}}, {'Y', {3, 28, 3}}, {'Z', {25, 21, 19}},
    {'?', {1, 21, 3}}, {'<', {4, 10, 17}}, {'>', {17, 10, 4}}, {'^', {2, 1, 2}},
}};

constexpr std::array<Glyph5x7, 45> kGlyphs5x7{{
    {' ', {0, 0, 0, 0, 0}}, {'-', {8, 8, 8, 8, 8}}, {'.', {0, 0, 0, 0, 16}},
    {'/', {16, 8, 4, 2, 1}}, {'>', {4, 2, 1, 2, 4}}, {'<', {4, 8, 16, 8, 4}},
    {'^', {4, 2, 31, 2, 4}},
    {'0', {31, 17, 17, 17, 31}}, {'1', {0, 18, 31, 16, 0}}, {'2', {18, 25, 21, 19, 18}},
    {'3', {17, 17, 21, 21, 31}}, {'4', {7, 4, 4, 31, 4}}, {'5', {23, 21, 21, 21, 13}},
    {'6', {15, 21, 21, 21, 9}}, {'7', {1, 1, 25, 5, 3}}, {'8', {10, 21, 21, 21, 10}},
    {'9', {18, 21, 21, 21, 30}},
    {'A', {30, 5, 5, 5, 30}}, {'B', {31, 21, 21, 21, 10}}, {'C', {14, 17, 17, 17, 10}},
    {'D', {31, 17, 17, 17, 14}}, {'E', {31, 21, 21, 21, 17}}, {'F', {31, 5, 5, 5, 1}},
    {'G', {14, 17, 21, 21, 28}}, {'H', {31, 4, 4, 4, 31}}, {'I', {17, 17, 31, 17, 17}},
    {'J', {8, 16, 16, 16, 15}}, {'K', {31, 4, 10, 17, 17}}, {'L', {31, 16, 16, 16, 16}},
    {'M', {31, 2, 4, 2, 31}}, {'N', {31, 2, 4, 8, 31}}, {'O', {14, 17, 17, 17, 14}},
    {'P', {31, 5, 5, 5, 2}}, {'Q', {14, 17, 21, 9, 22}}, {'R', {31, 5, 13, 21, 18}},
    {'S', {18, 21, 21, 21, 9}}, {'T', {1, 1, 31, 1, 1}}, {'U', {15, 16, 16, 16, 15}},
    {'V', {7, 8, 16, 8, 7}}, {'W', {31, 8, 4, 8, 31}}, {'X', {17, 10, 4, 10, 17}},
    {'Y', {1, 2, 28, 2, 1}}, {'Z', {17, 25, 21, 19, 17}}, {'?', {1, 1, 21, 5, 2}},
    {'!', {0, 0, 31, 0, 0}},
}};

std::array<std::uint8_t, 3> glyph_for(char character)
{
    if (character >= 'a' && character <= 'z') character = static_cast<char>(character - 'a' + 'A');
    for (const auto& glyph : kGlyphs) {
        if (glyph.character == character) return glyph.columns;
    }
    return {1, 21, 3};
}

std::array<std::uint8_t, 5> glyph5x7_for(char character)
{
    if (character >= 'a' && character <= 'z') character = static_cast<char>(character - 'a' + 'A');
    for (const auto& glyph : kGlyphs5x7) if (glyph.character == character) return glyph.columns;
    return {1, 1, 21, 5, 2};
}

}  // namespace

void Framebuffer::clear(const bool on) { data_.fill(on ? 0xFF : 0x00); }

bool Framebuffer::pixel(const int x, const int y) const
{
    if (x < 0 || x >= width || y < 0 || y >= height) return false;
    return (data_[static_cast<std::size_t>(y / 8) * width + x] & (1U << (y % 8))) != 0;
}

void Framebuffer::set_pixel(const int x, const int y, const bool on)
{
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    auto& byte = data_[static_cast<std::size_t>(y / 8) * width + x];
    const auto mask = static_cast<std::uint8_t>(1U << (y % 8));
    byte = on ? static_cast<std::uint8_t>(byte | mask) : static_cast<std::uint8_t>(byte & ~mask);
}

void Framebuffer::fill_rect(const int x, const int y, const int rect_width, const int rect_height, const bool on)
{
    for (int py = std::max(0, y); py < std::min(height, y + rect_height); ++py) {
        for (int px = std::max(0, x); px < std::min(width, x + rect_width); ++px) set_pixel(px, py, on);
    }
}

void Framebuffer::line(int x0, int y0, const int x1, const int y1, const bool on)
{
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        set_pixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        const int twice_error = error * 2;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void Framebuffer::thick_line(const int x0, const int y0, const int x1, const int y1,
                             const int thickness, const bool on)
{
    const int radius = std::max(0, thickness / 2);
    const bool mostly_horizontal = std::abs(x1 - x0) >= std::abs(y1 - y0);
    for (int offset = -radius; offset <= radius; ++offset) {
        line(x0 + (mostly_horizontal ? 0 : offset), y0 + (mostly_horizontal ? offset : 0),
             x1 + (mostly_horizontal ? 0 : offset), y1 + (mostly_horizontal ? offset : 0), on);
    }
}

void Framebuffer::text_3x5(int x, const int y, const std::string_view text, const bool on, const int spacing)
{
    for (const char character : text) {
        const auto glyph = glyph_for(character);
        for (int column = 0; column < 3; ++column) {
            for (int row = 0; row < 5; ++row) {
                if ((glyph[column] & (1U << row)) != 0) set_pixel(x + column, y + row, on);
            }
        }
        x += 3 + spacing;
    }
}

void Framebuffer::text_5x7(int x, const int y, const std::string_view text, const bool on, const int spacing)
{
    for (const char character : text) {
        const auto glyph = glyph5x7_for(character);
        for (int column = 0; column < 5; ++column) {
            for (int row = 0; row < 7; ++row) {
                if ((glyph[column] & (1U << row)) != 0) set_pixel(x + column, y + row, on);
            }
        }
        x += 5 + spacing;
    }
}

int Framebuffer::text_5x7_width(const std::string_view text, const int spacing)
{
    return text.empty() ? 0 : static_cast<int>(text.size()) * 5 + static_cast<int>(text.size() - 1) * spacing;
}

const std::array<std::uint8_t, Framebuffer::byte_count>& Framebuffer::bytes() const { return data_; }

}  // namespace bikeos
