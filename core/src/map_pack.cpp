#include "bikeos/map_pack.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace bikeos {
namespace {

constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kHeaderSize = 20;
constexpr std::size_t kIndexEntrySize = 32;

template <typename T>
bool read_le(std::span<const std::uint8_t> input, std::size_t& offset, T& value)
{
    if (offset + sizeof(T) > input.size()) return false;
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned bits = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) bits |= static_cast<Unsigned>(input[offset + i]) << (8U * i);
    value = static_cast<T>(bits);
    offset += sizeof(T);
    return true;
}

bool intersects(const MapTilePack::Tile& tile, const GeoPointE7 min, const GeoPointE7 max)
{
    return tile.maximum.latitude_e7 >= min.latitude_e7 && tile.minimum.latitude_e7 <= max.latitude_e7 &&
           tile.maximum.longitude_e7 >= min.longitude_e7 && tile.minimum.longitude_e7 <= max.longitude_e7;
}

}  // namespace

bool MapTilePack::open(const std::span<const std::uint8_t> bytes)
{
    bytes_ = {};
    tiles_.clear();
    if (bytes.size() < kHeaderSize || bytes[0] != 'B' || bytes[1] != 'P' || bytes[2] != 'K' || bytes[3] != '1') return false;
    std::size_t offset = 4;
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint32_t tile_count = 0;
    std::uint32_t index_size = 0;
    std::uint32_t index_crc = 0;
    if (!read_le(bytes, offset, version) || !read_le(bytes, offset, flags) || !read_le(bytes, offset, tile_count) ||
        !read_le(bytes, offset, index_size) || !read_le(bytes, offset, index_crc) || version != kVersion || flags != 0 ||
        tile_count == 0 || index_size != tile_count * kIndexEntrySize || kHeaderSize + index_size > bytes.size() ||
        crc32(bytes.subspan(kHeaderSize, index_size)) != index_crc) {
        return false;
    }
    std::vector<Tile> candidate;
    candidate.reserve(tile_count);
    offset = kHeaderSize;
    for (std::uint32_t i = 0; i < tile_count; ++i) {
        Tile tile;
        std::uint32_t reserved = 0;
        if (!read_le(bytes, offset, tile.minimum.latitude_e7) || !read_le(bytes, offset, tile.minimum.longitude_e7) ||
            !read_le(bytes, offset, tile.maximum.latitude_e7) || !read_le(bytes, offset, tile.maximum.longitude_e7) ||
            !read_le(bytes, offset, tile.offset) || !read_le(bytes, offset, tile.size) || !read_le(bytes, offset, tile.crc) ||
            !read_le(bytes, offset, reserved) || reserved != 0 || !valid_coordinate(tile.minimum) ||
            !valid_coordinate(tile.maximum) || tile.minimum.latitude_e7 > tile.maximum.latitude_e7 ||
            tile.minimum.longitude_e7 > tile.maximum.longitude_e7 || tile.offset < kHeaderSize + index_size ||
            tile.size == 0 || static_cast<std::size_t>(tile.offset) + tile.size > bytes.size() ||
            crc32(bytes.subspan(tile.offset, tile.size)) != tile.crc) {
            return false;
        }
        candidate.push_back(tile);
    }
    bytes_ = bytes;
    tiles_ = std::move(candidate);
    return true;
}

bool MapTilePack::covers(const GeoPointE7 point) const
{
    return std::any_of(tiles_.begin(), tiles_.end(), [&](const Tile& tile) {
        return point.latitude_e7 >= tile.minimum.latitude_e7 && point.latitude_e7 <= tile.maximum.latitude_e7 &&
               point.longitude_e7 >= tile.minimum.longitude_e7 && point.longitude_e7 <= tile.maximum.longitude_e7;
    });
}

bool MapTilePack::decode_nearby(const GeoPointE7 centre, const float radius_m, MapDataset& output) const
{
    if (!valid() || !valid_coordinate(centre) || !std::isfinite(radius_m) || radius_m <= 0.0F) return false;
    const double lat_delta = static_cast<double>(radius_m) / 111320.0;
    const double lon_delta = lat_delta / std::max(0.2, std::cos(centre.latitude_e7 * 1.0e-7 * 3.141592653589793 / 180.0));
    const GeoPointE7 min{static_cast<std::int32_t>((centre.latitude_e7 * 1.0e-7 - lat_delta) * 1.0e7),
                         static_cast<std::int32_t>((centre.longitude_e7 * 1.0e-7 - lon_delta) * 1.0e7)};
    const GeoPointE7 max{static_cast<std::int32_t>((centre.latitude_e7 * 1.0e-7 + lat_delta) * 1.0e7),
                         static_cast<std::int32_t>((centre.longitude_e7 * 1.0e-7 + lon_delta) * 1.0e7)};
    MapDataset merged;
    merged.version = 4;
    merged.origin = centre;
    merged.minimum = min;
    merged.maximum = max;
    bool loaded = false;
    for (const auto& tile : tiles_) {
        if (!intersects(tile, min, max)) continue;
        MapDataset decoded;
        if (!decode_map(bytes_.subspan(tile.offset, tile.size), decoded)) return false;
        merged.minimum.latitude_e7 = std::min(merged.minimum.latitude_e7, decoded.minimum.latitude_e7);
        merged.minimum.longitude_e7 = std::min(merged.minimum.longitude_e7, decoded.minimum.longitude_e7);
        merged.maximum.latitude_e7 = std::max(merged.maximum.latitude_e7, decoded.maximum.latitude_e7);
        merged.maximum.longitude_e7 = std::max(merged.maximum.longitude_e7, decoded.maximum.longitude_e7);
        merged.roads.insert(merged.roads.end(), decoded.roads.begin(), decoded.roads.end());
        merged.areas.insert(merged.areas.end(), decoded.areas.begin(), decoded.areas.end());
        loaded = true;
    }
    if (!loaded) return false;
    output = std::move(merged);
    return true;
}

}  // namespace bikeos
