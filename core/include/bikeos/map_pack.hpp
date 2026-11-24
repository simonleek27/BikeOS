#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "bikeos/map_data.hpp"

namespace bikeos {

// A compact, read-only collection of BMAP tiles. The pack stays in flash;
// decode_nearby copies only the tiles around the rider into the working map.
class MapTilePack {
public:
    bool open(std::span<const std::uint8_t> bytes);
    bool valid() const { return bytes_.data() != nullptr && !tiles_.empty(); }
    std::size_t tile_count() const { return tiles_.size(); }
    bool covers(GeoPointE7 point) const;

    // Decode all tiles intersecting the square around `centre`. The output is
    // intentionally a render-only working set and may contain several tiles.
    bool decode_nearby(GeoPointE7 centre, float radius_m, MapDataset& output) const;

private:
public:
    struct Tile {
        GeoPointE7 minimum{};
        GeoPointE7 maximum{};
        std::uint32_t offset{};
        std::uint32_t size{};
        std::uint32_t crc{};
    };

private:
    std::span<const std::uint8_t> bytes_{};
    std::vector<Tile> tiles_;
};

}  // namespace bikeos
