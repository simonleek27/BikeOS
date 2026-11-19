#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "bikeos/geo.hpp"

namespace bikeos {

enum class RoadClass : std::uint8_t {
    motorway,
    primary,
    secondary,
    residential,
    service,
    cycleway,
    footway,
    other,
};

struct RoadPolyline {
    std::uint32_t feature_id{};
    RoadClass road_class{RoadClass::other};
    std::string name;
    std::vector<GeoPointE7> points;
};

enum class AreaClass : std::uint8_t {
    water,
    park,
};

// A filled area. The first ring is the outline; further rings are holes
// (islands in a canal, a pond in a park). Filled with the even-odd rule.
struct AreaPolygon {
    AreaClass area_class{AreaClass::water};
    std::string name;
    std::vector<std::vector<GeoPointE7>> rings;
};

struct MapDataset {
    std::uint16_t version{4};
    GeoPointE7 origin{};
    GeoPointE7 minimum{};
    GeoPointE7 maximum{};
    std::vector<RoadPolyline> roads;
    std::vector<AreaPolygon> areas;  // version 4 and later

    std::size_t point_count() const;
    bool valid() const;
};

std::uint32_t crc32(std::span<const std::uint8_t> data);
std::vector<std::uint8_t> encode_map(const MapDataset& map);
bool decode_map(std::span<const std::uint8_t> encoded, MapDataset& output);

}  // namespace bikeos
