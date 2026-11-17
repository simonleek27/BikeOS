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

struct MapDataset {
    std::uint16_t version{3};
    GeoPointE7 origin{};
    GeoPointE7 minimum{};
    GeoPointE7 maximum{};
    std::vector<RoadPolyline> roads;

    std::size_t point_count() const;
    bool valid() const;
};

std::uint32_t crc32(std::span<const std::uint8_t> data);
std::vector<std::uint8_t> encode_map(const MapDataset& map);
bool decode_map(std::span<const std::uint8_t> encoded, MapDataset& output);

}  // namespace bikeos
