// Renders every color-screen state from the Amsterdam demo assets to PNGs so
// the TFT design can be checked without hardware.
//
//   bikeos_color_preview [output_dir] [scale]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "bikeos/canvas.hpp"
#include "bikeos/color_renderer.hpp"
#include "bikeos/geo.hpp"
#include "bikeos/gps.hpp"
#include "bikeos/map_data.hpp"
#include "bikeos/navigation.hpp"
#include "png_writer.hpp"

namespace {

// 2026-11-19 12:30 UTC (day) and 20:00 UTC (night) in Amsterdam.
constexpr std::int64_t kDayEpochMs = 1795091400000LL;
constexpr std::int64_t kNightEpochMs = 1795118400000LL;

std::vector<std::uint8_t> read_binary(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct Pose {
    bikeos::GeoPointE7 position;
    float heading_deg;
};

Pose along(const bikeos::Route& route, const float distance_m, const float sideways_m = 0.0F)
{
    const auto& cumulative = route.cumulative_distance_m;
    std::size_t segment = 1;
    while (segment + 1 < route.points.size() && cumulative[segment] < distance_m) ++segment;
    const auto a = route.points[segment - 1];
    const auto b = route.points[segment];
    const float length = std::max(0.01F, cumulative[segment] - cumulative[segment - 1]);
    const double t = std::clamp((distance_m - cumulative[segment - 1]) / length, 0.0F, 1.0F);
    const auto local = bikeos::project_local(b, a);
    const double heading = std::atan2(local.east_m, local.north_m);
    const double east = local.east_m * t + sideways_m * std::cos(heading);
    const double north = local.north_m * t - sideways_m * std::sin(heading);
    constexpr double radius = 6371008.8;
    constexpr double degrees = 180.0 / 3.14159265358979323846;
    const double latitude = a.latitude_e7 * 1.0e-7 / degrees;
    return {
        {a.latitude_e7 + static_cast<std::int32_t>(std::llround(north / radius * degrees * 1.0e7)),
         a.longitude_e7 + static_cast<std::int32_t>(std::llround(east / (radius * std::cos(latitude)) * degrees * 1.0e7))},
        static_cast<float>(bikeos::normalize_heading_deg(heading * degrees)),
    };
}

bikeos::GpsFix fix(const Pose pose, const std::uint64_t monotonic_ms, const std::int64_t epoch_ms)
{
    return {
        .position = pose.position,
        .utc_timestamp_ms = static_cast<std::uint64_t>(epoch_ms % 86400000LL),
        .utc_epoch_ms = epoch_ms,
        .monotonic_timestamp_ms = monotonic_ms,
        .speed_mps = 4.5F,
        .heading_deg = pose.heading_deg,
        .accuracy_m = 5.0F,
        .source = bikeos::GpsSource::simulated,
        .quality = 1,
        .position_valid = true,
        .heading_valid = true,
    };
}

// Rides the route up to `distance_m` (and optionally off to the side), the way
// the engine would see it, and returns the final snapshot.
bikeos::NavigationSnapshot ride_to(bikeos::NavigationEngine& engine, const bikeos::Route& route,
                                   const float distance_m, const std::int64_t epoch_ms,
                                   const float sideways_m = 0.0F, const int sideways_fixes = 0)
{
    bikeos::NavigationSnapshot snapshot;
    std::uint64_t now = 1000;
    for (float d = 0.0F; d < distance_m; d += 4.5F, now += 1000) {
        snapshot = engine.update(fix(along(route, d), now, epoch_ms), true, now);
    }
    for (int i = 0; i < sideways_fixes; ++i, now += 1000) {
        snapshot = engine.update(fix(along(route, distance_m, sideways_m), now, epoch_ms), true, now);
    }
    snapshot = engine.update(fix(along(route, distance_m, sideways_fixes ? sideways_m : 0.0F), now, epoch_ms),
                             true, now);
    return snapshot;
}

}  // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path output = argc > 1 ? argv[1] : "color-preview";
    const int scale = argc > 2 ? std::max(1, std::atoi(argv[2])) : 2;
    std::filesystem::create_directories(output);

    bikeos::MapDataset map;
    bikeos::Route route;
    if (!bikeos::decode_map(read_binary(BIKEOS_SOURCE_DIR "/data/amsterdam_demo.bmap"), map) ||
        !bikeos::decode_route(read_binary(BIKEOS_SOURCE_DIR "/data/amsterdam_demo.brte"), route) ||
        !route.prepare_and_validate()) {
        std::cerr << "Could not load the Amsterdam demo assets\n";
        return 1;
    }
    std::cout << "map v" << map.version << ": " << map.roads.size() << " roads, " << map.areas.size()
              << " areas; route " << route.total_distance_m << " m, " << route.maneuvers.size() << " maneuvers\n";

    // Distance of the first maneuver, to place the "approaching a turn" frame.
    const float first_turn = route.cumulative_distance_m[route.maneuvers.front().point_index];
    struct Scenario {
        std::string name;
        std::int64_t epoch;
        bikeos::NavigationSnapshot snapshot;
        int warmup_frames;
    };
    std::vector<Scenario> scenarios;
    const auto add = [&](const std::string& name, const std::int64_t epoch, const auto& make) {
        bikeos::NavigationEngine engine;
        engine.set_route(route);
        scenarios.push_back({name, epoch, make(engine), 8});
    };
    add("01-cruising-day", kDayEpochMs, [&](auto& e) { return ride_to(e, route, std::max(5.0F, first_turn - 180.0F), kDayEpochMs); });
    add("02-cruising-night", kNightEpochMs, [&](auto& e) { return ride_to(e, route, std::max(5.0F, first_turn - 180.0F), kNightEpochMs); });
    add("03-turn-close-day", kDayEpochMs, [&](auto& e) { return ride_to(e, route, first_turn - 45.0F, kDayEpochMs); });
    add("04-turn-close-night", kNightEpochMs, [&](auto& e) { return ride_to(e, route, first_turn - 45.0F, kNightEpochMs); });
    add("05-off-route-day", kDayEpochMs, [&](auto& e) {
        return ride_to(e, route, route.total_distance_m * 0.45F, kDayEpochMs, 55.0F, 6);
    });
    add("06-gps-lost-night", kNightEpochMs, [&](auto& e) {
        ride_to(e, route, route.total_distance_m * 0.6F, kNightEpochMs);
        return e.update(std::nullopt, true, 900000);
    });
    add("07-waiting-for-gps-day", 0, [&](auto& e) { return e.update(std::nullopt, true, 1000); });
    add("08-arrived-day", kDayEpochMs, [&](auto& e) { return ride_to(e, route, route.total_distance_m, kDayEpochMs); });
    {
        bikeos::NavigationEngine engine;  // no route loaded
        auto snapshot = engine.update(fix(along(route, 300.0F), 1000, kNightEpochMs), true, 1000);
        snapshot.gps_valid = true;
        snapshot.position = along(route, 300.0F).position;
        snapshot.heading_valid = true;
        snapshot.heading_deg = along(route, 300.0F).heading_deg;
        scenarios.push_back({"09-no-route-night", kNightEpochMs, snapshot, 1});
    }

    const std::vector<bikeos::GeoPointE7> no_route;
    for (const auto& scenario : scenarios) {
        bikeos::Canvas565 canvas;
        bikeos::ColorMapRenderer renderer;
        const bool routed = scenario.snapshot.state != bikeos::NavigationState::no_route;
        // Let the zoom easing settle the way it would after a few seconds of riding.
        const auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < scenario.warmup_frames; ++i) {
            renderer.render(canvas, map, routed ? std::span<const bikeos::GeoPointE7>(route.points) : no_route,
                            scenario.snapshot, {.utc_epoch_ms = scenario.epoch});
        }
        const double render_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() /
                                 scenario.warmup_frames;
        const auto path = output / (scenario.name + ".png");
        if (!write_canvas_png(path, canvas, scale)) {
            std::cerr << "Failed to write " << path << '\n';
            return 1;
        }
        std::cout << path.string() << "  state=" << bikeos::navigation_state_name(scenario.snapshot.state)
                  << " zoom=" << renderer.meters_per_pixel() << "m/px render=" << render_ms << "ms\n";
    }
    return 0;
}
