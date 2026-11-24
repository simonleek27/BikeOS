#include "bikeos/navigation.hpp"

#include <array>
#include <limits>
#include <type_traits>

#include "bikeos/map_data.hpp"

namespace bikeos {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'B', 'R', 'T', 'E'};
constexpr std::uint16_t kVersionV1 = 1;
constexpr std::uint16_t kVersionV2 = 2;
constexpr std::int32_t kQuantizationE7 = 10;
constexpr std::size_t kMaxEncodedRouteSize = 512U * 1024U;

template <typename T>
void append_le(std::vector<std::uint8_t>& output, T value)
{
    using Unsigned = std::make_unsigned_t<T>;
    const Unsigned bits = static_cast<Unsigned>(value);
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
    for (std::size_t i = 0; i < sizeof(T); ++i) bits |= static_cast<Unsigned>(input[offset + i]) << (i * 8U);
    value = static_cast<T>(bits);
    offset += sizeof(T);
    return true;
}

}  // namespace

std::vector<std::uint8_t> encode_route(const Route& input)
{
    Route route = input;
    if (!route.prepare_and_validate() || route.points.size() > 65535 || route.maneuvers.size() > 65535) return {};
    const GeoPointE7 origin = route.points.front();
    std::vector<std::uint8_t> payload;
    bool fits_v1 = true;
    for (const auto point : route.points) {
        const auto lat = (point.latitude_e7 - origin.latitude_e7) / kQuantizationE7;
        const auto lon = (point.longitude_e7 - origin.longitude_e7) / kQuantizationE7;
        if (lat < std::numeric_limits<std::int16_t>::min() || lat > std::numeric_limits<std::int16_t>::max() ||
            lon < std::numeric_limits<std::int16_t>::min() || lon > std::numeric_limits<std::int16_t>::max()) {
            fits_v1 = false;
            break;
        }
    }
    if (fits_v1) {
        for (const auto point : route.points) {
            append_le(payload, static_cast<std::int16_t>((point.latitude_e7 - origin.latitude_e7) / kQuantizationE7));
            append_le(payload, static_cast<std::int16_t>((point.longitude_e7 - origin.longitude_e7) / kQuantizationE7));
        }
    } else {
        // v2 uses absolute E7 coordinates. This removes the approximately
        // 65 km corridor limit of v1 without changing maneuver semantics.
        for (const auto point : route.points) {
            append_le(payload, point.latitude_e7);
            append_le(payload, point.longitude_e7);
        }
    }
    for (const auto& maneuver : route.maneuvers) {
        if (maneuver.point_index > 65535 || maneuver.street_name.size() > 96) return {};
        append_le(payload, static_cast<std::uint16_t>(maneuver.point_index));
        append_le(payload, static_cast<std::uint8_t>(maneuver.type));
        append_le(payload, static_cast<std::uint8_t>(maneuver.street_name.size()));
        payload.insert(payload.end(), maneuver.street_name.begin(), maneuver.street_name.end());
    }
    if (payload.size() + 32U > kMaxEncodedRouteSize) return {};

    std::vector<std::uint8_t> encoded;
    encoded.insert(encoded.end(), kMagic.begin(), kMagic.end());
    append_le(encoded, fits_v1 ? kVersionV1 : kVersionV2);
    append_le(encoded, static_cast<std::uint16_t>(0));
    append_le(encoded, route.route_id);
    append_le(encoded, origin.latitude_e7);
    append_le(encoded, origin.longitude_e7);
    append_le(encoded, static_cast<std::uint16_t>(route.points.size()));
    append_le(encoded, static_cast<std::uint16_t>(route.maneuvers.size()));
    append_le(encoded, static_cast<std::uint32_t>(payload.size()));
    append_le(encoded, crc32(payload));
    encoded.insert(encoded.end(), payload.begin(), payload.end());
    return encoded;
}

bool decode_route(const std::span<const std::uint8_t> encoded, Route& output)
{
    constexpr std::size_t header_size = 32;
    if (encoded.size() < header_size || !std::equal(kMagic.begin(), kMagic.end(), encoded.begin())) return false;
    std::size_t offset = 4;
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint16_t point_count = 0;
    std::uint16_t maneuver_count = 0;
    std::uint32_t payload_size = 0;
    std::uint32_t expected_crc = 0;
    GeoPointE7 origin{};
    Route route;
    if (!read_le(encoded, offset, version) || !read_le(encoded, offset, flags) ||
        !read_le(encoded, offset, route.route_id) || !read_le(encoded, offset, origin.latitude_e7) ||
        !read_le(encoded, offset, origin.longitude_e7) || !read_le(encoded, offset, point_count) ||
        !read_le(encoded, offset, maneuver_count) || !read_le(encoded, offset, payload_size) ||
        !read_le(encoded, offset, expected_crc)) {
        return false;
    }
    if ((version != kVersionV1 && version != kVersionV2) || flags != 0 || point_count < 2 ||
        payload_size != encoded.size() - offset || encoded.size() > kMaxEncodedRouteSize ||
        crc32(encoded.subspan(offset)) != expected_crc) {
        return false;
    }
    route.version = version;
    route.points.reserve(point_count);
    for (std::uint16_t i = 0; i < point_count; ++i) {
        if (version == kVersionV1) {
            std::int16_t lat = 0;
            std::int16_t lon = 0;
            if (!read_le(encoded, offset, lat) || !read_le(encoded, offset, lon)) return false;
            route.points.push_back({origin.latitude_e7 + lat * kQuantizationE7,
                                    origin.longitude_e7 + lon * kQuantizationE7});
        } else {
            GeoPointE7 point{};
            if (!read_le(encoded, offset, point.latitude_e7) || !read_le(encoded, offset, point.longitude_e7) ||
                !valid_coordinate(point)) return false;
            route.points.push_back(point);
        }
    }
    route.maneuvers.reserve(maneuver_count);
    for (std::uint16_t i = 0; i < maneuver_count; ++i) {
        std::uint16_t point_index = 0;
        std::uint8_t type = 0;
        std::uint8_t name_length = 0;
        if (!read_le(encoded, offset, point_index) || !read_le(encoded, offset, type) ||
            !read_le(encoded, offset, name_length) || type > static_cast<std::uint8_t>(ManeuverType::destination) ||
            name_length > 96 || offset + name_length > encoded.size()) {
            return false;
        }
        route.maneuvers.push_back({
            .point_index = point_index,
            .type = static_cast<ManeuverType>(type),
            .street_name = std::string(reinterpret_cast<const char*>(encoded.data() + offset), name_length),
        });
        offset += name_length;
    }
    if (offset != encoded.size() || !route.prepare_and_validate()) return false;
    output = std::move(route);
    return true;
}

}  // namespace bikeos
