#include "bikeos/gps_simulator.hpp"

#include <algorithm>
#include <cmath>

#include "bikeos/geo.hpp"

namespace bikeos {
namespace {

constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
constexpr double kEarthRadiusM = 6371008.8;

}  // namespace

GpsSimulator::GpsSimulator(const std::uint32_t seed) : random_state_(seed == 0 ? 1 : seed) {}

bool GpsSimulator::set_route(const Route& route)
{
    if (route.points.size() < 2 || route.cumulative_distance_m.size() != route.points.size()) return false;
    route_ = &route;
    reset();
    return true;
}

void GpsSimulator::set_speed_mps(const float speed_mps) { speed_mps_ = std::clamp(speed_mps, 0.0F, 20.0F); }
void GpsSimulator::set_events(std::vector<SimulationEvent> events) { events_ = std::move(events); }
void GpsSimulator::reset() { progress_m_ = 0.0F; }
float GpsSimulator::progress_m() const { return progress_m_; }
bool GpsSimulator::finished() const { return route_ && progress_m_ >= route_->total_distance_m; }

float GpsSimulator::noise(const float amplitude_m)
{
    random_state_ = random_state_ * 1664525U + 1013904223U;
    const float unit = static_cast<float>((random_state_ >> 8U) & 0x00FFFFFFU) / 16777215.0F;
    return (unit * 2.0F - 1.0F) * amplitude_m;
}

SimulationEventType GpsSimulator::active_event(const float progress, float& magnitude_m) const
{
    for (const auto& event : events_) {
        if (progress >= event.start_progress_m && progress <= event.end_progress_m) {
            magnitude_m = event.magnitude_m;
            return event.type;
        }
    }
    magnitude_m = 0.0F;
    return SimulationEventType::normal;
}

GeoPointE7 GpsSimulator::position_at_progress(const float progress, const float lateral_offset_m) const
{
    const float clamped = std::clamp(progress, 0.0F, route_->total_distance_m);
    const auto upper = std::upper_bound(route_->cumulative_distance_m.begin(),
                                        route_->cumulative_distance_m.end(), clamped);
    const std::size_t next = std::clamp<std::size_t>(
        static_cast<std::size_t>(upper - route_->cumulative_distance_m.begin()), 1, route_->points.size() - 1);
    const std::size_t previous = next - 1;
    const float segment_length = route_->cumulative_distance_m[next] - route_->cumulative_distance_m[previous];
    const double t = segment_length > 0.0F
                         ? (clamped - route_->cumulative_distance_m[previous]) / segment_length
                         : 0.0;
    const auto a = route_->points[previous];
    const auto b = route_->points[next];
    GeoPointE7 point{
        .latitude_e7 = static_cast<std::int32_t>(std::llround(a.latitude_e7 + (b.latitude_e7 - a.latitude_e7) * t)),
        .longitude_e7 = static_cast<std::int32_t>(std::llround(a.longitude_e7 + (b.longitude_e7 - a.longitude_e7) * t)),
    };
    if (lateral_offset_m != 0.0F) {
        const LocalPointM segment = project_local(b, a);
        const double length = std::hypot(segment.east_m, segment.north_m);
        if (length > 0.1) {
            const double east_offset = -segment.north_m / length * lateral_offset_m;
            const double north_offset = segment.east_m / length * lateral_offset_m;
            const double lat_delta = north_offset / kEarthRadiusM / kDegreesToRadians;
            const double lon_delta = east_offset / (kEarthRadiusM * std::cos(point.latitude_e7 * 1.0e-7 * kDegreesToRadians)) /
                                     kDegreesToRadians;
            point.latitude_e7 += static_cast<std::int32_t>(std::llround(lat_delta * 1.0e7));
            point.longitude_e7 += static_cast<std::int32_t>(std::llround(lon_delta * 1.0e7));
        }
    }
    return point;
}

std::optional<GpsFix> GpsSimulator::step(const std::uint64_t monotonic_timestamp_ms,
                                         const float elapsed_seconds)
{
    if (!route_) return std::nullopt;
    float magnitude = 0.0F;
    const SimulationEventType event = active_event(progress_m_, magnitude);
    if (event != SimulationEventType::stop) {
        progress_m_ = std::min(route_->total_distance_m, progress_m_ + speed_mps_ * std::max(0.0F, elapsed_seconds));
    }
    if (event == SimulationEventType::gps_loss) return std::nullopt;

    float lateral = noise(1.8F);
    if (event == SimulationEventType::off_route) lateral += magnitude;
    if (event == SimulationEventType::jump) lateral += magnitude;
    const GeoPointE7 position = position_at_progress(progress_m_, lateral);
    const GeoPointE7 ahead = position_at_progress(std::min(route_->total_distance_m, progress_m_ + 4.0F), lateral);
    const LocalPointM direction = project_local(ahead, position);
    const float heading = static_cast<float>(normalize_heading_deg(
        std::atan2(direction.east_m, direction.north_m) / kDegreesToRadians));
    return GpsFix{
        .position = position,
        .utc_timestamp_ms = utc_base_ms_ + monotonic_timestamp_ms,
        .monotonic_timestamp_ms = monotonic_timestamp_ms,
        .speed_mps = event == SimulationEventType::stop ? 0.0F : speed_mps_,
        .heading_deg = heading,
        .accuracy_m = 4.0F,
        .source = GpsSource::simulated,
        .quality = 1,
        .position_valid = true,
        .heading_valid = event != SimulationEventType::stop && speed_mps_ >= 0.5F,
    };
}

}  // namespace bikeos
