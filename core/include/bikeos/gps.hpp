#pragma once

#include <cstdint>
#include <optional>

#include "bikeos/geo.hpp"

namespace bikeos {

enum class GpsSource : std::uint8_t {
    simulated,
    uart_nmea,
    iphone,
};

struct GpsFix {
    GeoPointE7 position{};
    std::uint64_t utc_timestamp_ms{};
    // Milliseconds since 1970-01-01 UTC, or 0 when the receiver has not
    // reported a date yet. utc_timestamp_ms is time of day only for NMEA.
    std::int64_t utc_epoch_ms{};
    std::uint64_t monotonic_timestamp_ms{};
    float speed_mps{};
    float heading_deg{};
    float accuracy_m{1000.0F};
    GpsSource source{GpsSource::simulated};
    std::uint8_t quality{};
    bool position_valid{};
    bool heading_valid{};
};

struct GpsFilterConfig {
    std::uint64_t stale_after_ms{3500};
    float maximum_speed_mps{35.0F};
    float jump_allowance_m{30.0F};
};

class GpsFilter {
public:
    explicit GpsFilter(GpsFilterConfig config = {});

    std::optional<GpsFix> accept(const GpsFix& candidate);
    bool stale(std::uint64_t now_monotonic_ms) const;
    const std::optional<GpsFix>& last_fix() const;
    void reset();

private:
    GpsFilterConfig config_;
    std::optional<GpsFix> last_fix_;
};

}  // namespace bikeos
