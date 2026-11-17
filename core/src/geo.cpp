#include "bikeos/geo.hpp"

#include <cmath>

namespace bikeos {
namespace {

constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
constexpr double kEarthRadiusM = 6371008.8;
constexpr double kE7ToDegrees = 1.0e-7;

}  // namespace

bool valid_coordinate(const GeoPointE7 point)
{
    return point.latitude_e7 >= -900000000 && point.latitude_e7 <= 900000000 &&
           point.longitude_e7 >= -1800000000 && point.longitude_e7 <= 1800000000;
}

LocalPointM project_local(const GeoPointE7 point, const GeoPointE7 origin)
{
    const double origin_lat_rad = origin.latitude_e7 * kE7ToDegrees * kDegreesToRadians;
    const double delta_lat_rad = (point.latitude_e7 - origin.latitude_e7) * kE7ToDegrees * kDegreesToRadians;
    const double delta_lon_rad = (point.longitude_e7 - origin.longitude_e7) * kE7ToDegrees * kDegreesToRadians;
    return {
        .east_m = delta_lon_rad * std::cos(origin_lat_rad) * kEarthRadiusM,
        .north_m = delta_lat_rad * kEarthRadiusM,
    };
}

double distance_m(const GeoPointE7 a, const GeoPointE7 b)
{
    const LocalPointM local = project_local(b, a);
    return std::hypot(local.east_m, local.north_m);
}

double normalize_heading_deg(double heading_deg)
{
    heading_deg = std::fmod(heading_deg, 360.0);
    return heading_deg < 0.0 ? heading_deg + 360.0 : heading_deg;
}

}  // namespace bikeos
