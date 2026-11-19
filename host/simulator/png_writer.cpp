#include "png_writer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <span>
#include <vector>

#include <zlib.h>

namespace {

void append_be32(std::vector<std::uint8_t>& output, const std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

void append_chunk(std::vector<std::uint8_t>& png,
                  const std::array<char, 4>& type,
                  const std::span<const std::uint8_t> data)
{
    append_be32(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t crc_start = png.size();
    png.insert(png.end(), type.begin(), type.end());
    png.insert(png.end(), data.begin(), data.end());
    const auto checksum = static_cast<std::uint32_t>(::crc32(0, png.data() + crc_start,
                                                             static_cast<uInt>(png.size() - crc_start)));
    append_be32(png, checksum);
}

}  // namespace

static bool write_rgb_png(const std::filesystem::path& path, const std::uint32_t width, const std::uint32_t height,
                   const std::vector<std::uint8_t>& raw)
{
    uLongf compressed_size = compressBound(raw.size());
    std::vector<std::uint8_t> compressed(compressed_size);
    if (compress2(compressed.data(), &compressed_size, raw.data(), raw.size(), Z_BEST_COMPRESSION) != Z_OK) {
        return false;
    }
    compressed.resize(compressed_size);

    std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<std::uint8_t> header;
    append_be32(header, width);
    append_be32(header, height);
    header.insert(header.end(), {8, 2, 0, 0, 0});
    append_chunk(png, {'I', 'H', 'D', 'R'}, header);
    append_chunk(png, {'I', 'D', 'A', 'T'}, compressed);
    append_chunk(png, {'I', 'E', 'N', 'D'}, {});

    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return output.good();
}

bool write_framebuffer_png(const std::filesystem::path& path,
                           const bikeos::Framebuffer& framebuffer,
                           const int requested_scale)
{
    const int scale = std::max(1, requested_scale);
    const std::uint32_t width = bikeos::Framebuffer::width * scale;
    const std::uint32_t height = bikeos::Framebuffer::height * scale;
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1 + width * 3));
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        for (std::uint32_t x = 0; x < width; ++x) {
            const bool on = framebuffer.pixel(static_cast<int>(x / scale), static_cast<int>(y / scale));
            const std::array<std::uint8_t, 3> color = on
                ? std::array<std::uint8_t, 3>{238, 246, 255}
                : std::array<std::uint8_t, 3>{9, 40, 94};
            raw.insert(raw.end(), color.begin(), color.end());
        }
    }
    return write_rgb_png(path, width, height, raw);
}

bool write_canvas_png(const std::filesystem::path& path,
                      const bikeos::Canvas565& canvas,
                      const int requested_scale)
{
    const int scale = std::max(1, requested_scale);
    const auto width = static_cast<std::uint32_t>(canvas.width() * scale);
    const auto height = static_cast<std::uint32_t>(canvas.height() * scale);
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1 + width * 3));
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint16_t pixel = canvas.pixel(static_cast<int>(x / scale), static_cast<int>(y / scale));
            // Expand 5/6/5 bits back to 8 bits the way the panel shows them.
            const auto red = static_cast<std::uint8_t>(((pixel >> 11U) & 0x1FU) * 255U / 31U);
            const auto green = static_cast<std::uint8_t>(((pixel >> 5U) & 0x3FU) * 255U / 63U);
            const auto blue = static_cast<std::uint8_t>((pixel & 0x1FU) * 255U / 31U);
            raw.insert(raw.end(), {red, green, blue});
        }
    }
    return write_rgb_png(path, width, height, raw);
}
