#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "bikeos/gps.hpp"

namespace bikeos {

enum class ManeuverType : std::uint8_t {
    straight,
    left,
    right,
    slight_left,
    slight_right,
    sharp_left,
    sharp_right,
    u_turn,
    roundabout,
    destination,
};

enum class NavigationState : std::uint8_t {
    no_route,
    waiting_for_gps,
    navigating,
    gps_lost,
    off_route,
    rerouting,
    phone_disconnected,
    destination_reached,
    error,
};

struct Maneuver {
    std::uint32_t point_index{};
    ManeuverType type{ManeuverType::straight};
    std::string street_name;
};

struct Route {
    std::uint32_t route_id{};
    std::uint16_t version{1};
    std::vector<GeoPointE7> points;
    std::vector<float> cumulative_distance_m;
    std::vector<Maneuver> maneuvers;
    float total_distance_m{};

    bool prepare_and_validate();
};

struct NavigationConfig {
    float off_route_threshold_m{30.0F};
    std::uint8_t off_route_confirm_fixes{5};
    std::uint64_t reroute_throttle_ms{12000};
    float arrival_radius_m{18.0F};
    float backwards_tolerance_m{25.0F};
    std::size_t forward_search_segments{80};
    std::size_t backward_search_segments{12};
};

struct NavigationSnapshot {
    NavigationState state{NavigationState::no_route};
    std::uint32_t route_id{};
    GeoPointE7 position{};
    GeoPointE7 matched_position{};
    std::size_t matched_segment{};
    float cross_track_distance_m{};
    float route_progress_m{};
    float route_remaining_m{};
    std::optional<Maneuver> next_maneuver;
    float distance_to_maneuver_m{};
    float heading_deg{};
    bool gps_valid{};
    bool heading_valid{};
    bool phone_connected{};
    bool reroute_requested{};
};

class NavigationEngine {
public:
    explicit NavigationEngine(NavigationConfig config = {});

    bool set_route(Route route);
    void clear_route();
    NavigationSnapshot update(const std::optional<GpsFix>& fix,
                              bool phone_connected,
                              std::uint64_t now_monotonic_ms);
    void mark_rerouting();
    const NavigationSnapshot& snapshot() const;
    const std::optional<Route>& route() const;

private:
    struct MatchResult {
        std::size_t segment{};
        double t{};
        float cross_track_m{};
        float progress_m{};
        GeoPointE7 projected{};
    };

    std::optional<MatchResult> match_position(GeoPointE7 position) const;
    void update_next_maneuver();

    NavigationConfig config_;
    std::optional<Route> route_;
    NavigationSnapshot snapshot_;
    std::uint8_t deviating_fixes_{};
    std::uint64_t last_reroute_request_ms_{};
    bool rerouting_{};
};

const char* navigation_state_name(NavigationState state);
const char* maneuver_name(ManeuverType maneuver);
std::vector<std::uint8_t> encode_route(const Route& route);
bool decode_route(std::span<const std::uint8_t> encoded, Route& output);

}  // namespace bikeos
