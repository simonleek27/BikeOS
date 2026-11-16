#include "bikeos/nmea.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace bikeos {
namespace {

std::vector<std::string_view> split_fields(std::string_view payload)
{
    std::vector<std::string_view> fields;
    while (true) {
        const auto comma = payload.find(',');
        fields.push_back(payload.substr(0, comma));
        if (comma == std::string_view::npos) break;
        payload.remove_prefix(comma + 1);
    }
    return fields;
}

std::optional<double> parse_decimal(const std::string_view text)
{
    if (text.empty() || text.size() > 24) return std::nullopt;
    std::array<char, 25> buffer{};
    std::copy(text.begin(), text.end(), buffer.begin());
    char* end = nullptr;
    const double value = std::strtod(buffer.data(), &end);
    if (end != buffer.data() + text.size() || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::optional<GeoPointE7> parse_position(const std::string_view latitude,
                                         const std::string_view north_south,
                                         const std::string_view longitude,
                                         const std::string_view east_west)
{
    const auto raw_lat = parse_decimal(latitude);
    const auto raw_lon = parse_decimal(longitude);
    if (!raw_lat || !raw_lon || (north_south != "N" && north_south != "S") ||
        (east_west != "E" && east_west != "W")) {
        return std::nullopt;
    }
    const double lat_deg = std::floor(*raw_lat / 100.0);
    const double lon_deg = std::floor(*raw_lon / 100.0);
    double latitude_deg = lat_deg + (*raw_lat - lat_deg * 100.0) / 60.0;
    double longitude_deg = lon_deg + (*raw_lon - lon_deg * 100.0) / 60.0;
    if (north_south == "S") latitude_deg = -latitude_deg;
    if (east_west == "W") longitude_deg = -longitude_deg;
    const GeoPointE7 point{
        .latitude_e7 = static_cast<std::int32_t>(std::llround(latitude_deg * 1.0e7)),
        .longitude_e7 = static_cast<std::int32_t>(std::llround(longitude_deg * 1.0e7)),
    };
    return valid_coordinate(point) ? std::optional(point) : std::nullopt;
}

std::optional<std::uint64_t> parse_utc_time_ms(const std::string_view hhmmss)
{
    const auto raw = parse_decimal(hhmmss);
    if (!raw || hhmmss.size() < 6) return std::nullopt;
    const unsigned hour = static_cast<unsigned>(*raw / 10000.0);
    const unsigned minute = static_cast<unsigned>((*raw - hour * 10000.0) / 100.0);
    const double seconds = *raw - hour * 10000.0 - minute * 100.0;
    if (hour > 23 || minute > 59 || seconds < 0.0 || seconds >= 60.0) return std::nullopt;
    return static_cast<std::uint64_t>(std::llround((hour * 3600.0 + minute * 60.0 + seconds) * 1000.0));
}

}  // namespace

bool NmeaParser::checksum_valid(const std::string_view sentence)
{
    if (sentence.size() < 7 || sentence.front() != '$') return false;
    const auto star = sentence.find('*');
    if (star == std::string_view::npos || star + 2 >= sentence.size()) return false;
    std::uint8_t checksum = 0;
    for (std::size_t i = 1; i < star; ++i) checksum ^= static_cast<std::uint8_t>(sentence[i]);
    unsigned expected = 0;
    const auto result = std::from_chars(sentence.data() + star + 1, sentence.data() + star + 3, expected, 16);
    return result.ec == std::errc{} && result.ptr == sentence.data() + star + 3 && checksum == expected;
}

std::optional<GpsFix> NmeaParser::parse(const std::string_view sentence,
                                        const std::uint64_t monotonic_timestamp_ms)
{
    if (!checksum_valid(sentence)) return std::nullopt;
    const auto star = sentence.find('*');
    const auto fields = split_fields(sentence.substr(1, star - 1));
    if (fields.empty() || fields[0].size() < 3) return std::nullopt;
    const std::string_view type = fields[0].substr(fields[0].size() - 3);

    if (type == "RMC") {
        if (fields.size() < 10) return std::nullopt;
        const auto position = parse_position(fields[3], fields[4], fields[5], fields[6]);
        const auto time_ms = parse_utc_time_ms(fields[1]);
        const auto speed_knots = parse_decimal(fields[7]);
        const auto heading = fields[8].empty() ? std::optional<double>{} : parse_decimal(fields[8]);
        if (!position || !time_ms || !speed_knots) return std::nullopt;
        GpsFix fix{
            .position = *position,
            .utc_timestamp_ms = *time_ms,
            .monotonic_timestamp_ms = monotonic_timestamp_ms,
            .speed_mps = static_cast<float>(*speed_knots * 0.514444),
            .heading_deg = static_cast<float>(heading.value_or(0.0)),
            .accuracy_m = 15.0F,
            .source = GpsSource::uart_nmea,
            .quality = static_cast<std::uint8_t>(fields[2] == "A" ? 1 : 0),
            .position_valid = fields[2] == "A",
            .heading_valid = heading.has_value() && *speed_knots >= 0.5,
        };
        last_rmc_ = fix;
        return fix;
    }

    if (type == "GGA") {
        if (fields.size() < 10) return std::nullopt;
        const auto position = parse_position(fields[2], fields[3], fields[4], fields[5]);
        const auto time_ms = parse_utc_time_ms(fields[1]);
        const auto quality = parse_decimal(fields[6]);
        const auto hdop = parse_decimal(fields[8]);
        if (!position || !time_ms || !quality) return std::nullopt;
        GpsFix fix = last_rmc_.value_or(GpsFix{});
        fix.position = *position;
        fix.utc_timestamp_ms = *time_ms;
        fix.monotonic_timestamp_ms = monotonic_timestamp_ms;
        fix.accuracy_m = static_cast<float>(hdop.value_or(10.0) * 5.0);
        fix.source = GpsSource::uart_nmea;
        fix.quality = static_cast<std::uint8_t>(std::clamp(*quality, 0.0, 8.0));
        fix.position_valid = *quality >= 1.0;
        return fix;
    }
    return std::nullopt;
}

}  // namespace bikeos
