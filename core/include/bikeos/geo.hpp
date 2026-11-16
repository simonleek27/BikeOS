#pragma once

#include <cstdint>

namespace bikeos {

struct GeoPointE7 {
    std::int32_t latitude_e7{};
    std::int32_t longitude_e7{};

    friend bool operator==(const GeoPointE7&, const GeoPointE7&) = default;
};

struct LocalPointM {
    double east_m{};
    double north_m{};
};

bool valid_coordinate(GeoPointE7 point);
LocalPointM project_local(GeoPointE7 point, GeoPointE7 origin);
double distance_m(GeoPointE7 a, GeoPointE7 b);
double normalize_heading_deg(double heading_deg);

}  // namespace bikeos
