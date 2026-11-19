#include "bikeos/navigation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "bikeos/geo.hpp"

namespace bikeos {
namespace {

GeoPointE7 interpolate_geo(const GeoPointE7 a, const GeoPointE7 b, const double t)
{
    return {
        .latitude_e7 = static_cast<std::int32_t>(std::llround(a.latitude_e7 + (b.latitude_e7 - a.latitude_e7) * t)),
        .longitude_e7 = static_cast<std::int32_t>(std::llround(a.longitude_e7 + (b.longitude_e7 - a.longitude_e7) * t)),
    };
}

}  // namespace

bool Route::prepare_and_validate()
{
    if (version == 0 || route_id == 0 || points.size() < 2 || points.size() > 100000) return false;
    if (!std::all_of(points.begin(), points.end(), valid_coordinate)) return false;

    cumulative_distance_m.assign(points.size(), 0.0F);
    for (std::size_t i = 1; i < points.size(); ++i) {
        const double segment = distance_m(points[i - 1], points[i]);
        if (!std::isfinite(segment) || segment < 0.05 || segment > 5000.0) return false;
        cumulative_distance_m[i] = cumulative_distance_m[i - 1] + static_cast<float>(segment);
    }
    total_distance_m = cumulative_distance_m.back();
    if (total_distance_m <= 0.0F) return false;
    std::uint32_t previous_index = 0;
    for (const auto& maneuver : maneuvers) {
        if (maneuver.point_index >= points.size() || maneuver.point_index < previous_index ||
            maneuver.street_name.size() > 96) {
            return false;
        }
        previous_index = maneuver.point_index;
    }
    return true;
}

NavigationEngine::NavigationEngine(const NavigationConfig config) : config_(config) {}

bool NavigationEngine::set_route(Route route)
{
    if (!route.prepare_and_validate()) return false;
    route_ = std::move(route);
    snapshot_ = {};
    snapshot_.route_id = route_->route_id;
    snapshot_.state = NavigationState::waiting_for_gps;
    snapshot_.route_remaining_m = route_->total_distance_m;
    deviating_fixes_ = 0;
    rerouting_ = false;
    return true;
}

void NavigationEngine::clear_route()
{
    route_.reset();
    snapshot_ = {};
    deviating_fixes_ = 0;
    rerouting_ = false;
}

std::optional<NavigationEngine::MatchResult> NavigationEngine::match_position(const GeoPointE7 position) const
{
    if (!route_ || route_->points.size() < 2) return std::nullopt;
    const std::size_t segment_count = route_->points.size() - 1;
    std::size_t first = 0;
    std::size_t last = segment_count;
    if (snapshot_.gps_valid) {
        first = snapshot_.matched_segment > config_.backward_search_segments
                    ? snapshot_.matched_segment - config_.backward_search_segments
                    : 0;
        last = std::min(segment_count, snapshot_.matched_segment + config_.forward_search_segments + 1);
    }

    double best_score = std::numeric_limits<double>::infinity();
    MatchResult best{};
    bool found = false;
    for (std::size_t i = first; i < last; ++i) {
        const LocalPointM a = project_local(route_->points[i], position);
        const LocalPointM b = project_local(route_->points[i + 1], position);
        const double dx = b.east_m - a.east_m;
        const double dy = b.north_m - a.north_m;
        const double length_squared = dx * dx + dy * dy;
        if (length_squared < 0.01) continue;
        const double t = std::clamp(-(a.east_m * dx + a.north_m * dy) / length_squared, 0.0, 1.0);
        const double nearest_x = a.east_m + t * dx;
        const double nearest_y = a.north_m + t * dy;
        const double cross_track = std::hypot(nearest_x, nearest_y);
        const float progress = route_->cumulative_distance_m[i] +
                               static_cast<float>(t * std::sqrt(length_squared));
        double score = cross_track;
        if (snapshot_.gps_valid && progress + config_.backwards_tolerance_m < snapshot_.route_progress_m) {
            score += (snapshot_.route_progress_m - progress) * 2.0;
        }
        if (score < best_score) {
            best_score = score;
            best = {
                .segment = i,
                .t = t,
                .cross_track_m = static_cast<float>(cross_track),
                .progress_m = progress,
                .projected = interpolate_geo(route_->points[i], route_->points[i + 1], t),
            };
            found = true;
        }
    }
    return found ? std::optional(best) : std::nullopt;
}

void NavigationEngine::update_next_maneuver()
{
    snapshot_.next_maneuver.reset();
    snapshot_.following_maneuver.reset();
    snapshot_.following_maneuver_gap_m = 0.0F;
    snapshot_.distance_to_maneuver_m = snapshot_.route_remaining_m;
    if (!route_) return;
    const auto& maneuvers = route_->maneuvers;
    for (std::size_t i = 0; i < maneuvers.size(); ++i) {
        const float maneuver_progress = route_->cumulative_distance_m[maneuvers[i].point_index];
        if (maneuver_progress + 5.0F >= snapshot_.route_progress_m) {
            snapshot_.next_maneuver = maneuvers[i];
            snapshot_.distance_to_maneuver_m = std::max(0.0F, maneuver_progress - snapshot_.route_progress_m);
            if (i + 1 < maneuvers.size()) {
                snapshot_.following_maneuver = maneuvers[i + 1];
                snapshot_.following_maneuver_gap_m = std::max(
                    0.0F, route_->cumulative_distance_m[maneuvers[i + 1].point_index] - maneuver_progress);
            }
            return;
        }
    }
}

NavigationSnapshot NavigationEngine::update(const std::optional<GpsFix>& fix,
                                             const bool phone_connected,
                                             const std::uint64_t now_monotonic_ms)
{
    snapshot_.phone_connected = phone_connected;
    snapshot_.reroute_requested = false;
    if (!route_) {
        snapshot_.state = NavigationState::no_route;
        return snapshot_;
    }
    if (!fix || !fix->position_valid) {
        snapshot_.gps_valid = false;
        snapshot_.state = snapshot_.route_progress_m > 0.0F ? NavigationState::gps_lost
                                                            : NavigationState::waiting_for_gps;
        return snapshot_;
    }
    const auto match = match_position(fix->position);
    if (!match) {
        snapshot_.state = NavigationState::error;
        return snapshot_;
    }

    snapshot_.position = fix->position;
    snapshot_.matched_position = match->projected;
    snapshot_.matched_segment = match->segment;
    snapshot_.cross_track_distance_m = match->cross_track_m;
    snapshot_.route_progress_m = std::max(snapshot_.route_progress_m, match->progress_m);
    snapshot_.route_remaining_m = std::max(0.0F, route_->total_distance_m - snapshot_.route_progress_m);
    snapshot_.gps_valid = true;
    snapshot_.heading_deg = fix->heading_deg;
    snapshot_.heading_valid = fix->heading_valid;
    update_next_maneuver();

    const float deviation_limit = std::max(config_.off_route_threshold_m, fix->accuracy_m * 1.5F);
    if (match->cross_track_m > deviation_limit) {
        deviating_fixes_ = static_cast<std::uint8_t>(std::min<int>(255, deviating_fixes_ + 1));
    } else {
        deviating_fixes_ = 0;
        rerouting_ = false;
    }

    if (snapshot_.route_remaining_m <= config_.arrival_radius_m &&
        distance_m(fix->position, route_->points.back()) <= config_.arrival_radius_m * 1.5F) {
        snapshot_.state = NavigationState::destination_reached;
    } else if (deviating_fixes_ >= config_.off_route_confirm_fixes) {
        snapshot_.state = rerouting_ ? NavigationState::rerouting : NavigationState::off_route;
        const bool throttle_expired = last_reroute_request_ms_ == 0 ||
                                      now_monotonic_ms - last_reroute_request_ms_ >= config_.reroute_throttle_ms;
        if (phone_connected && throttle_expired) {
            snapshot_.reroute_requested = true;
            last_reroute_request_ms_ = now_monotonic_ms;
            rerouting_ = true;
            snapshot_.state = NavigationState::rerouting;
        } else if (!phone_connected) {
            snapshot_.state = NavigationState::phone_disconnected;
        }
    } else {
        snapshot_.state = phone_connected ? NavigationState::navigating
                                          : NavigationState::phone_disconnected;
    }
    return snapshot_;
}

void NavigationEngine::set_phone_connected(const bool connected)
{
    snapshot_.phone_connected = connected;
    if (!connected && snapshot_.state != NavigationState::no_route &&
        snapshot_.state != NavigationState::destination_reached && snapshot_.state != NavigationState::error) {
        snapshot_.state = NavigationState::phone_disconnected;
    } else if (connected && snapshot_.state == NavigationState::phone_disconnected) {
        snapshot_.state = snapshot_.gps_valid ? NavigationState::navigating : NavigationState::waiting_for_gps;
    }
}

void NavigationEngine::mark_rerouting()
{
    rerouting_ = true;
    snapshot_.state = NavigationState::rerouting;
}

const NavigationSnapshot& NavigationEngine::snapshot() const { return snapshot_; }
const std::optional<Route>& NavigationEngine::route() const { return route_; }

const char* navigation_state_name(const NavigationState state)
{
    switch (state) {
        case NavigationState::no_route: return "NO_ROUTE";
        case NavigationState::waiting_for_gps: return "WAITING_GPS";
        case NavigationState::navigating: return "NAVIGATING";
        case NavigationState::gps_lost: return "GPS_LOST";
        case NavigationState::off_route: return "OFF_ROUTE";
        case NavigationState::rerouting: return "REROUTING";
        case NavigationState::phone_disconnected: return "NO_PHONE";
        case NavigationState::destination_reached: return "ARRIVED";
        case NavigationState::error: return "ERROR";
    }
    return "UNKNOWN";
}

const char* maneuver_name(const ManeuverType maneuver)
{
    switch (maneuver) {
        case ManeuverType::straight: return "STRAIGHT";
        case ManeuverType::left: return "LEFT";
        case ManeuverType::right: return "RIGHT";
        case ManeuverType::slight_left: return "SLIGHT_LEFT";
        case ManeuverType::slight_right: return "SLIGHT_RIGHT";
        case ManeuverType::sharp_left: return "SHARP_LEFT";
        case ManeuverType::sharp_right: return "SHARP_RIGHT";
        case ManeuverType::u_turn: return "U_TURN";
        case ManeuverType::roundabout: return "ROUNDABOUT";
        case ManeuverType::destination: return "DESTINATION";
    }
    return "UNKNOWN";
}

}  // namespace bikeos
