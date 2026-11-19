#include "bikeos/map_data.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <type_traits>

namespace bikeos {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'B', 'M', 'A', 'P'};
constexpr std::uint16_t kFormatVersion = 4;
constexpr std::int32_t kQuantizationE7 = 10;  // 1e-6 degrees.

template <typename T>
void append_le(std::vector<std::uint8_t>& output, T value)
{
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned bits = static_cast<Unsigned>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        output.push_back(static_cast<std::uint8_t>((bits >> (i * 8U)) & 0xFFU));
    }
}

template <typename T>
bool read_le(std::span<const std::uint8_t> input, std::size_t& offset, T& value)
{
    if (offset + sizeof(T) > input.size()) return false;
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned bits = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bits |= static_cast<Unsigned>(input[offset + i]) << (i * 8U);
    }
    value = static_cast<T>(bits);
    offset += sizeof(T);
    return true;
}

}  // namespace

std::size_t MapDataset::point_count() const
{
    std::size_t count = 0;
    for (const auto& road : roads) count += road.points.size();
    return count;
}

bool MapDataset::valid() const
{
    if ((version < 1 || version > kFormatVersion) || !valid_coordinate(origin) || !valid_coordinate(minimum) ||
        !valid_coordinate(maximum) || minimum.latitude_e7 > maximum.latitude_e7 ||
        minimum.longitude_e7 > maximum.longitude_e7 || roads.size() > 65535 ||
        point_count() > 1000000) {
        return false;
    }
    const auto fits = [this](const GeoPointE7 point) {
        if (!valid_coordinate(point)) return false;
        const auto lat_offset = (point.latitude_e7 - origin.latitude_e7) / kQuantizationE7;
        const auto lon_offset = (point.longitude_e7 - origin.longitude_e7) / kQuantizationE7;
        return lat_offset >= std::numeric_limits<std::int16_t>::min() &&
               lat_offset <= std::numeric_limits<std::int16_t>::max() &&
               lon_offset >= std::numeric_limits<std::int16_t>::min() &&
               lon_offset <= std::numeric_limits<std::int16_t>::max();
    };
    for (const auto& road : roads) {
        if (road.points.size() < 2 || road.points.size() > 65535 || road.name.size() > 96 ||
            !std::all_of(road.points.begin(), road.points.end(), valid_coordinate)) {
            return false;
        }
        if (!std::all_of(road.points.begin(), road.points.end(), fits)) return false;
    }
    if (areas.size() > 65535 || (version < 4 && !areas.empty())) return false;
    std::size_t area_points = 0;
    for (const auto& area : areas) {
        if (area.rings.empty() || area.rings.size() > 255 || area.name.size() > 96 ||
            static_cast<std::uint8_t>(area.area_class) > 1) {
            return false;
        }
        for (const auto& ring : area.rings) {
            if (ring.size() < 3 || ring.size() > 65535 || !std::all_of(ring.begin(), ring.end(), fits)) {
                return false;
            }
            area_points += ring.size();
        }
    }
    return area_points <= 1000000;
}

std::uint32_t crc32(const std::span<const std::uint8_t> data)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

std::vector<std::uint8_t> encode_map(const MapDataset& map)
{
    if (!map.valid()) return {};
    std::vector<std::uint8_t> payload;
    std::vector<std::string> names;
    names.reserve(map.roads.size());
    const auto intern = [&names](const std::string& name) {
        if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    };
    for (const auto& road : map.roads) intern(road.name);
    for (const auto& area : map.areas) intern(area.name);
    if (names.size() > 65535) return {};
    payload.reserve(map.point_count() * 4 + map.roads.size() * 9 + names.size() * 8);
    append_le(payload, static_cast<std::uint16_t>(names.size()));
    for (const auto& name : names) {
        append_le(payload, static_cast<std::uint8_t>(name.size()));
        payload.insert(payload.end(), name.begin(), name.end());
    }
    const auto append_point = [&](const GeoPointE7 point) {
        append_le(payload, static_cast<std::int16_t>((point.latitude_e7 - map.origin.latitude_e7) / kQuantizationE7));
        append_le(payload, static_cast<std::int16_t>((point.longitude_e7 - map.origin.longitude_e7) / kQuantizationE7));
    };
    for (const auto& road : map.roads) {
        append_le(payload, static_cast<std::uint8_t>(road.road_class));
        append_le(payload, road.feature_id);
        append_le(payload, static_cast<std::uint16_t>(road.points.size()));
        append_le(payload, static_cast<std::uint16_t>(std::find(names.begin(), names.end(), road.name) - names.begin()));
        for (const auto point : road.points) append_point(point);
    }
    append_le(payload, static_cast<std::uint16_t>(map.areas.size()));
    for (const auto& area : map.areas) {
        append_le(payload, static_cast<std::uint8_t>(area.area_class));
        append_le(payload, static_cast<std::uint16_t>(std::find(names.begin(), names.end(), area.name) - names.begin()));
        append_le(payload, static_cast<std::uint8_t>(area.rings.size()));
        for (const auto& ring : area.rings) {
            append_le(payload, static_cast<std::uint16_t>(ring.size()));
            for (const auto point : ring) append_point(point);
        }
    }

    std::vector<std::uint8_t> output;
    output.insert(output.end(), kMagic.begin(), kMagic.end());
    append_le(output, kFormatVersion);
    append_le(output, static_cast<std::uint16_t>(0));
    append_le(output, map.origin.latitude_e7);
    append_le(output, map.origin.longitude_e7);
    append_le(output, map.minimum.latitude_e7);
    append_le(output, map.minimum.longitude_e7);
    append_le(output, map.maximum.latitude_e7);
    append_le(output, map.maximum.longitude_e7);
    append_le(output, static_cast<std::uint32_t>(map.roads.size()));
    append_le(output, static_cast<std::uint32_t>(map.point_count()));
    append_le(output, static_cast<std::uint32_t>(payload.size()));
    append_le(output, crc32(payload));
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

bool decode_map(const std::span<const std::uint8_t> encoded, MapDataset& output)
{
    constexpr std::size_t header_size = 48;
    if (encoded.size() < header_size || !std::equal(kMagic.begin(), kMagic.end(), encoded.begin())) return false;
    std::size_t offset = 4;
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint32_t road_count = 0;
    std::uint32_t expected_points = 0;
    std::uint32_t payload_size = 0;
    std::uint32_t expected_crc = 0;
    MapDataset candidate;
    if (!read_le(encoded, offset, version) || !read_le(encoded, offset, flags) ||
        !read_le(encoded, offset, candidate.origin.latitude_e7) ||
        !read_le(encoded, offset, candidate.origin.longitude_e7) ||
        !read_le(encoded, offset, candidate.minimum.latitude_e7) ||
        !read_le(encoded, offset, candidate.minimum.longitude_e7) ||
        !read_le(encoded, offset, candidate.maximum.latitude_e7) ||
        !read_le(encoded, offset, candidate.maximum.longitude_e7) ||
        !read_le(encoded, offset, road_count) || !read_le(encoded, offset, expected_points) ||
        !read_le(encoded, offset, payload_size) || !read_le(encoded, offset, expected_crc)) {
        return false;
    }
    if ((version < 1 || version > kFormatVersion) || flags != 0 || road_count > 65535 || expected_points > 1000000 ||
        payload_size != encoded.size() - offset || crc32(encoded.subspan(offset)) != expected_crc) {
        return false;
    }
    candidate.version = version;
    candidate.roads.reserve(road_count);
    std::size_t decoded_points = 0;
    std::vector<std::string> names;
    if (version >= 3) {
        std::uint16_t name_count = 0;
        if (!read_le(encoded, offset, name_count)) return false;
        names.reserve(name_count);
        for (std::uint16_t i = 0; i < name_count; ++i) {
            std::uint8_t name_length = 0;
            if (!read_le(encoded, offset, name_length) || name_length > 96 || offset + name_length > encoded.size()) {
                return false;
            }
            names.emplace_back(reinterpret_cast<const char*>(encoded.data() + offset), name_length);
            offset += name_length;
        }
    }
    for (std::uint32_t road_index = 0; road_index < road_count; ++road_index) {
        std::uint8_t road_class = 0;
        std::uint8_t name_length = 0;
        std::uint32_t feature_id = 0;
        std::uint16_t point_count = 0;
        std::uint16_t name_index = 0;
        if (!read_le(encoded, offset, road_class)) return false;
        if (version == 1) {
            if (!read_le(encoded, offset, name_length) || !read_le(encoded, offset, point_count) || name_length != 0) {
                return false;
            }
        } else if (version == 2) {
            if (!read_le(encoded, offset, name_length) || !read_le(encoded, offset, feature_id) ||
                !read_le(encoded, offset, point_count) || name_length > 96 || offset + name_length > encoded.size()) {
                return false;
            }
        } else {
            if (!read_le(encoded, offset, feature_id) || !read_le(encoded, offset, point_count) ||
                !read_le(encoded, offset, name_index) || name_index >= names.size()) {
                return false;
            }
        }
        if (point_count < 2 || road_class > 7) return false;
        RoadPolyline road;
        road.feature_id = feature_id;
        road.road_class = static_cast<RoadClass>(road_class);
        if (version == 2) {
            road.name = std::string(reinterpret_cast<const char*>(encoded.data() + offset), name_length);
            offset += name_length;
        } else if (version >= 3) {
            road.name = names[name_index];
        } else {
            // Version 1 used the second byte as a reserved field and had no
            // names. Keep old assets decodable for deliberate migration.
            if (name_length != 0) return false;
        }
        road.points.reserve(point_count);
        for (std::uint16_t point_index = 0; point_index < point_count; ++point_index) {
            std::int16_t lat_offset = 0;
            std::int16_t lon_offset = 0;
            if (!read_le(encoded, offset, lat_offset) || !read_le(encoded, offset, lon_offset)) return false;
            road.points.push_back({
                .latitude_e7 = candidate.origin.latitude_e7 + lat_offset * kQuantizationE7,
                .longitude_e7 = candidate.origin.longitude_e7 + lon_offset * kQuantizationE7,
            });
        }
        decoded_points += point_count;
        candidate.roads.push_back(std::move(road));
    }
    if (version >= 4) {
        std::uint16_t area_count = 0;
        if (!read_le(encoded, offset, area_count)) return false;
        candidate.areas.reserve(area_count);
        for (std::uint16_t area_index = 0; area_index < area_count; ++area_index) {
            std::uint8_t area_class = 0;
            std::uint16_t name_index = 0;
            std::uint8_t ring_count = 0;
            if (!read_le(encoded, offset, area_class) || !read_le(encoded, offset, name_index) ||
                !read_le(encoded, offset, ring_count) || area_class > 1 || name_index >= names.size() ||
                ring_count == 0) {
                return false;
            }
            AreaPolygon area;
            area.area_class = static_cast<AreaClass>(area_class);
            area.name = names[name_index];
            area.rings.resize(ring_count);
            for (auto& ring : area.rings) {
                std::uint16_t ring_points = 0;
                if (!read_le(encoded, offset, ring_points) || ring_points < 3) return false;
                ring.reserve(ring_points);
                for (std::uint16_t i = 0; i < ring_points; ++i) {
                    std::int16_t lat_offset = 0;
                    std::int16_t lon_offset = 0;
                    if (!read_le(encoded, offset, lat_offset) || !read_le(encoded, offset, lon_offset)) return false;
                    ring.push_back({
                        .latitude_e7 = candidate.origin.latitude_e7 + lat_offset * kQuantizationE7,
                        .longitude_e7 = candidate.origin.longitude_e7 + lon_offset * kQuantizationE7,
                    });
                }
            }
            candidate.areas.push_back(std::move(area));
        }
    }
    if (offset != encoded.size() || decoded_points != expected_points || !candidate.valid()) return false;
    output = std::move(candidate);
    return true;
}

}  // namespace bikeos
