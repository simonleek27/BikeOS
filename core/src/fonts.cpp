#include "bikeos/fonts.hpp"

#include <algorithm>

namespace bikeos {

const Glyph* Font::find(const std::uint32_t codepoint) const
{
    const Glyph* end = glyphs + glyph_count;
    const Glyph* found = std::lower_bound(glyphs, end, codepoint, [](const Glyph& glyph, const std::uint32_t value) {
        return glyph.codepoint < value;
    });
    return (found != end && found->codepoint == codepoint) ? found : nullptr;
}

std::uint32_t next_codepoint(const std::string_view utf8, std::size_t& index)
{
    const auto byte = [&](const std::size_t i) { return static_cast<std::uint8_t>(utf8[i]); };
    const std::uint8_t lead = byte(index);
    if (lead < 0x80U) {
        ++index;
        return lead;
    }
    std::size_t length = 0;
    std::uint32_t value = 0;
    if ((lead & 0xE0U) == 0xC0U) {
        length = 2;
        value = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3;
        value = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4;
        value = lead & 0x07U;
    } else {
        ++index;
        return '?';
    }
    if (index + length > utf8.size()) {
        index = utf8.size();
        return '?';
    }
    for (std::size_t i = 1; i < length; ++i) {
        const std::uint8_t continuation = byte(index + i);
        if ((continuation & 0xC0U) != 0x80U) {
            index += i;
            return '?';
        }
        value = (value << 6U) | (continuation & 0x3FU);
    }
    index += length;
    return value;
}

int text_width(const Font& font, const std::string_view utf8)
{
    int width = 0;
    std::size_t index = 0;
    while (index < utf8.size()) {
        const Glyph* glyph = font.find(next_codepoint(utf8, index));
        if (!glyph) glyph = font.find('?');
        if (glyph) width += glyph->advance;
    }
    return width;
}

std::string fit_text(const Font& font, const std::string_view utf8, const int max_width)
{
    if (text_width(font, utf8) <= max_width) return std::string(utf8);
    constexpr std::string_view ellipsis = "\xE2\x80\xA6";
    const int ellipsis_width = text_width(font, ellipsis);
    std::size_t index = 0;
    std::size_t fits = 0;
    int width = 0;
    while (index < utf8.size()) {
        std::size_t next = index;
        const Glyph* glyph = font.find(next_codepoint(utf8, next));
        if (!glyph) glyph = font.find('?');
        const int advance = glyph ? glyph->advance : 0;
        if (width + advance + ellipsis_width > max_width) break;
        width += advance;
        index = next;
        fits = index;
    }
    std::string result(utf8.substr(0, fits));
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result + std::string(ellipsis);
}

}  // namespace bikeos
