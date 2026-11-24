// BikeOS virtual screen engine.
//
// Runs the real core code (NavigationEngine, GpsFilter, ColorMapRenderer,
// Canvas565) the way firmware/main/navigation_runtime.cpp does on the ESP32-S3:
// navigation steps once per second from GPS fixes, and display frames glide
// between steps. The test bench server (server.py) drives it over stdin/stdout.
//
// Protocol: one command per line on stdin. Every reply starts with a header
// line, `JSON <n>` followed by n bytes of JSON, or `FRAME <n> <m>` followed by
// n bytes of status JSON and m bytes of RGB565 pixels (320x240, little endian,
// row-major), exactly the buffer the TFT driver sends to the panel.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>
#include <sstream>
#include <string>
#include <vector>

#include "bikeos/canvas.hpp"
#include "bikeos/color_renderer.hpp"
#include "bikeos/geo.hpp"
#include "bikeos/gps.hpp"
#include "bikeos/map_data.hpp"
#include "bikeos/navigation.hpp"

namespace {

using namespace bikeos;

constexpr float kDemoSpeedMps = 4.5F;              // firmware kDemoSpeedMps
constexpr float kOffRouteEventLengthM = 45.0F;     // firmware trigger_off_route
constexpr float kOffRouteOffsetM = 50.0F;
constexpr std::uint64_t kStepPeriodMs = 1000;      // firmware kStepPeriodMs

std::vector<std::uint8_t> read_binary(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string json_escape(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) < 0x20) continue;
        out += c;
    }
    return out;
}

const char* run_state_name(const NavigationRunState state)
{
    switch (state) {
    case NavigationRunState::not_started: return "not_started";
    case NavigationRunState::running: return "running";
    case NavigationRunState::paused: return "paused";
    case NavigationRunState::stopped: return "stopped";
    case NavigationRunState::arrived: return "arrived";
    }
    return "unknown";
}

std::int64_t wall_clock_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

class Bench {
public:
    explicit Bench(const std::filesystem::path& data_dir) : data_dir_(data_dir)
    {
        if (!decode_map(read_binary(data_dir_ / "amsterdam_demo.bmap"), demo_map_)) {
            throw std::runtime_error("cannot load data/amsterdam_demo.bmap");
        }
        std::string error;
        // Boot like the firmware: demo map and route loaded, ride not started.
        if (!install(data_dir_ / "amsterdam_demo.brte", true, error)) throw std::runtime_error(error);
    }

    bool install(const std::filesystem::path& brte, const bool with_map, std::string& error)
    {
        Route candidate;
        if (!decode_route(read_binary(brte), candidate)) {
            error = "route file is not a valid BRTE";
            return false;
        }
        MapDataset map;
        if (with_map) {
            map = demo_map_;
        } else {
            // Same as NavigationRuntime::install_bundle for a route-only bundle:
            // a bounding box around the route and no roads or areas.
            map.version = 4;
            map.origin = candidate.points.front();
            map.minimum = candidate.points.front();
            map.maximum = candidate.points.front();
            for (const auto point : candidate.points) {
                map.minimum.latitude_e7 = std::min(map.minimum.latitude_e7, point.latitude_e7);
                map.minimum.longitude_e7 = std::min(map.minimum.longitude_e7, point.longitude_e7);
                map.maximum.latitude_e7 = std::max(map.maximum.latitude_e7, point.latitude_e7);
                map.maximum.longitude_e7 = std::max(map.maximum.longitude_e7, point.longitude_e7);
            }
        }
        if (!navigation_.set_route(candidate)) {
            error = "route failed validation";
            return false;
        }
        map_ = std::move(map);
        route_ = std::move(candidate);
        route_.prepare_and_validate();
        filter_.reset();
        navigation_.set_phone_connected(phone_connected_);
        navigation_.set_run_state(NavigationRunState::not_started);
        latest_ = navigation_.snapshot();
        progress_m_ = 0.0F;
        auto_follow_ = false;
        off_route_event_ = {};
        route_with_map_ = with_map;
        return true;
    }

    std::string command(const std::string& name, std::string& error)
    {
        // Mirrors NavigationRuntime::handle_navigation_command in GPS-simulator
        // mode, where starting the ride also starts the rider moving.
        if (name == "start" || name == "resume") {
            if (navigation_.snapshot().run_state == NavigationRunState::arrived) {
                error = "already arrived; restart the route";
                return {};
            }
            navigation_.set_run_state(NavigationRunState::running);
            auto_follow_ = true;
        } else if (name == "pause") {
            navigation_.set_run_state(NavigationRunState::paused);
            auto_follow_ = false;
        } else if (name == "stop") {
            navigation_.set_run_state(NavigationRunState::stopped);
            auto_follow_ = false;
        } else if (name == "restart") {
            filter_.reset();
            navigation_.set_route(route_);
            navigation_.set_phone_connected(phone_connected_);
            navigation_.set_run_state(NavigationRunState::running);
            progress_m_ = 0.0F;
            off_route_event_ = {};
            auto_follow_ = true;
        } else if (name == "offroute") {
            const float start = progress_m_ + 1.0F;
            off_route_event_ = {start, std::min(route_.total_distance_m, start + kOffRouteEventLengthM)};
        } else {
            error = "unknown command " + name;
            return {};
        }
        latest_ = navigation_.snapshot();
        return "{\"ok\":true}";
    }

    void seek(const float progress_m)
    {
        const float target = std::clamp(progress_m, 0.0F, route_.total_distance_m);
        const bool backwards = target < progress_m_ - 1.0F;
        const bool jump = target - progress_m_ > 30.0F;
        progress_m_ = target;
        if (!backwards && !jump) return;
        // A real rider cannot teleport; rebuild the engine state by riding the
        // route up to the new point so matching, maneuvers and arrival agree.
        const NavigationRunState run_state = navigation_.snapshot().run_state;
        navigation_.set_route(route_);
        navigation_.set_phone_connected(phone_connected_);
        std::uint64_t t = 1;
        for (float d = 0.0F; d < target; d += kDemoSpeedMps, ++t) {
            navigation_.update(make_fix(d, 0.0F, true, t), phone_connected_, t);
        }
        navigation_.set_run_state(run_state == NavigationRunState::arrived ? NavigationRunState::running : run_state);
        filter_.reset();
        latest_ = navigation_.snapshot();
        force_step_ = true;
    }

    // Advances simulated time to now_ms, takes 1 Hz navigation steps, and
    // renders one display frame like NavigationRuntime::render_frame.
    std::string frame(const std::uint64_t now_ms, std::vector<std::uint8_t>& pixels)
    {
        if (last_advance_ms_ == 0 || now_ms < last_advance_ms_) last_advance_ms_ = now_ms;
        if (now_ms < last_step_ms_) last_step_ms_ = 0;  // clock restarted; step at once
        const float dt = static_cast<float>(now_ms - std::min(now_ms, last_advance_ms_)) / 1000.0F;
        last_advance_ms_ = now_ms;
        if (auto_follow_) progress_m_ = std::min(route_.total_distance_m, progress_m_ + speed_mps_ * std::min(dt, 0.5F));
        bool stepped = false;
        if (force_step_ || last_step_ms_ == 0 || now_ms - last_step_ms_ >= kStepPeriodMs) {
            step(now_ms);
            last_step_ms_ = now_ms;
            force_step_ = false;
            stepped = true;
        }
        const auto started = std::chrono::steady_clock::now();
        renderer_.render(canvas_, map_, route_.points, latest_,
                         {.utc_epoch_ms = clock_ms(), .theme = theme_, .now_ms = smooth_ ? now_ms : 0});
        const double render_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        const auto source = canvas_.pixels();
        pixels.resize(source.size() * 2);
        for (std::size_t i = 0; i < source.size(); ++i) {
            pixels[i * 2] = static_cast<std::uint8_t>(source[i] & 0xFFU);
            pixels[i * 2 + 1] = static_cast<std::uint8_t>(source[i] >> 8U);
        }
        return status_json(render_ms, stepped);
    }

    std::string set(const std::string& key, const std::string& value, std::string& error)
    {
        if (key == "auto") auto_follow_ = value == "1";
        else if (key == "speed") speed_mps_ = std::clamp(std::stof(value), 0.0F, 15.0F);
        else if (key == "phone") {
            phone_connected_ = value == "1";
            navigation_.set_phone_connected(phone_connected_);
            latest_ = navigation_.snapshot();
        } else if (key == "gps") gps_available_ = value == "1";
        else if (key == "offroute") forced_off_route_ = value == "1";
        else if (key == "noise") noise_ = value == "1";
        else if (key == "smooth") smooth_ = value == "1";
        else if (key == "theme") theme_ = value == "day" ? ThemeMode::day : value == "night" ? ThemeMode::night : ThemeMode::automatic;
        else if (key == "clock") {
            // "live" follows the computer clock; "none" means no GPS time yet;
            // a number is a fixed UTC epoch in ms that then runs forward.
            if (value == "live") clock_mode_ = ClockMode::live;
            else if (value == "none") {
                clock_mode_ = ClockMode::none;
                have_gps_time_ = false;
            }
            else {
                clock_mode_ = ClockMode::fixed;
                clock_base_ms_ = std::stoll(value);
                clock_set_at_ = std::chrono::steady_clock::now();
            }
        } else {
            error = "unknown setting " + key;
            return {};
        }
        return "{\"ok\":true}";
    }

    std::string map_json() const
    {
        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(6);
        const auto point = [&](const GeoPointE7 p) { out << '[' << p.latitude_e7 * 1e-7 << ',' << p.longitude_e7 * 1e-7 << ']'; };
        out << "{\"bounds\":[";
        point(demo_map_.minimum);
        out << ',';
        point(demo_map_.maximum);
        out << "],\"roads\":[";
        bool first = true;
        for (const auto& road : demo_map_.roads) {
            if (road.road_class == RoadClass::footway || road.road_class == RoadClass::service ||
                road.road_class == RoadClass::other) {
                continue;
            }
            out << (first ? "" : ",") << "{\"c\":" << static_cast<int>(road.road_class) << ",\"p\":[";
            for (std::size_t i = 0; i < road.points.size(); ++i) {
                if (i) out << ',';
                point(road.points[i]);
            }
            out << "]}";
            first = false;
        }
        out << "],\"areas\":[";
        first = true;
        for (const auto& area : demo_map_.areas) {
            out << (first ? "" : ",") << "{\"c\":" << static_cast<int>(area.area_class) << ",\"p\":[";
            const auto& ring = area.rings.front();
            for (std::size_t i = 0; i < ring.size(); ++i) {
                if (i) out << ',';
                point(ring[i]);
            }
            out << "]}";
            first = false;
        }
        out << "]}";
        return out.str();
    }

private:
    enum class ClockMode { live, fixed, none };

    // Wall-clock time the simulated GPS reports.
    std::int64_t gps_epoch_ms() const
    {
        switch (clock_mode_) {
        case ClockMode::live: return wall_clock_ms();
        case ClockMode::fixed:
            return clock_base_ms_ + std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - clock_set_at_)
                                        .count();
        case ClockMode::none: return 0;
        }
        return 0;
    }

    // The device clock comes from GPS fixes; it stays unknown until a fix
    // with a date has been accepted.
    std::int64_t clock_ms() const { return have_gps_time_ ? gps_epoch_ms() : 0; }

    float noise_m()
    {
        noise_state_ = noise_state_ * 1664525U + 1013904223U;
        return (static_cast<float>(noise_state_ >> 8U) / static_cast<float>(1U << 24U) - 0.5F) * 3.0F;
    }

    GpsFix make_fix(const float progress_m, const float sideways_m, const bool moving, const std::uint64_t now_ms)
    {
        const auto& cumulative = route_.cumulative_distance_m;
        std::size_t segment = 1;
        while (segment + 1 < route_.points.size() && cumulative[segment] < progress_m) ++segment;
        const auto a = route_.points[segment - 1];
        const auto b = route_.points[segment];
        const float length = std::max(0.01F, cumulative[segment] - cumulative[segment - 1]);
        const double t = std::clamp((progress_m - cumulative[segment - 1]) / length, 0.0F, 1.0F);
        const auto local = project_local(b, a);
        const double heading = std::atan2(local.east_m, local.north_m);
        const double east = local.east_m * t + sideways_m * std::cos(heading);
        const double north = local.north_m * t - sideways_m * std::sin(heading);
        constexpr double radius = 6371008.8;
        constexpr double degrees = 180.0 / 3.14159265358979323846;
        const double latitude = a.latitude_e7 * 1.0e-7 / degrees;
        const std::int64_t epoch = gps_epoch_ms();
        return {
            .position = {a.latitude_e7 + static_cast<std::int32_t>(std::llround(north / radius * degrees * 1.0e7)),
                         a.longitude_e7 + static_cast<std::int32_t>(std::llround(east / (radius * std::cos(latitude)) * degrees * 1.0e7))},
            .utc_timestamp_ms = static_cast<std::uint64_t>(std::max<std::int64_t>(0, epoch) % 86400000LL),
            .utc_epoch_ms = epoch,
            .monotonic_timestamp_ms = now_ms,
            .speed_mps = moving ? speed_mps_ : 0.0F,
            .heading_deg = static_cast<float>(normalize_heading_deg(heading * degrees)),
            .accuracy_m = 5.0F,
            .source = GpsSource::simulated,
            .quality = 1,
            .position_valid = true,
            // NMEA course is only meaningful while moving.
            .heading_valid = moving && speed_mps_ >= 0.5F,
        };
    }

    void step(const std::uint64_t now_ms)
    {
        std::optional<GpsFix> candidate;
        if (gps_available_) {
            const bool event = progress_m_ >= off_route_event_.first && progress_m_ < off_route_event_.second;
            const float sideways = (forced_off_route_ || event) ? kOffRouteOffsetM : 0.0F;
            // Optional jitter of about +/-1.5 m along and across the route, like a real receiver.
            const float along = noise_ ? noise_m() : 0.0F;
            const float across = noise_ ? noise_m() : 0.0F;
            candidate = make_fix(std::max(0.0F, progress_m_ + along), sideways + across, auto_follow_, now_ms);
        }
        std::optional<GpsFix> accepted;
        if (candidate) accepted = filter_.accept(*candidate);
        if (accepted && accepted->utc_epoch_ms != 0) have_gps_time_ = true;
        latest_ = navigation_.update(accepted, phone_connected_, now_ms);
        // Firmware pauses the simulator once the ride has arrived.
        if (latest_.run_state == NavigationRunState::arrived) auto_follow_ = false;
        ++steps_;
    }

    std::string status_json(const double render_ms, const bool stepped) const
    {
        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(1);
        const auto& n = latest_;
        out << "{\"progress\":" << progress_m_ << ",\"total\":" << route_.total_distance_m
            << ",\"route_points\":" << route_.points.size() << ",\"route_id\":" << route_.route_id
            << ",\"state\":\"" << navigation_state_name(n.state) << "\",\"run_state\":\"" << run_state_name(n.run_state)
            << "\",\"engine_progress\":" << n.route_progress_m << ",\"remaining\":" << n.route_remaining_m
            << ",\"to_maneuver\":" << n.distance_to_maneuver_m << ",\"cross_track\":" << n.cross_track_distance_m
            << ",\"gps_valid\":" << (n.gps_valid ? "true" : "false")
            << ",\"phone\":" << (phone_connected_ ? "true" : "false")
            << ",\"auto\":" << (auto_follow_ ? "true" : "false") << ",\"speed\":" << speed_mps_
            << ",\"with_map\":" << (route_with_map_ ? "true" : "false")
            << ",\"zoom\":" << renderer_.meters_per_pixel() << ",\"render_ms\":" << render_ms
            << ",\"stepped\":" << (stepped ? "true" : "false") << ",\"steps\":" << steps_
            << ",\"reroute_requested\":" << (n.reroute_requested ? "true" : "false");
        out.precision(6);
        if (valid_coordinate(n.position)) {
            out << ",\"rider\":[" << n.position.latitude_e7 * 1e-7 << ',' << n.position.longitude_e7 * 1e-7 << ']';
        }
        out.precision(1);
        if (n.next_maneuver) {
            out << ",\"next\":{\"type\":\"" << maneuver_name(n.next_maneuver->type) << "\",\"street\":\""
                << json_escape(n.next_maneuver->street_name) << "\"}";
        }
        out << ",\"route\":[";
        out.precision(6);
        for (std::size_t i = 0; i < route_.points.size(); ++i) {
            out << (i ? "," : "") << '[' << route_.points[i].latitude_e7 * 1e-7 << ',' << route_.points[i].longitude_e7 * 1e-7 << ']';
        }
        out.precision(1);
        out << "],\"maneuvers\":[";
        for (std::size_t i = 0; i < route_.maneuvers.size(); ++i) {
            const auto& m = route_.maneuvers[i];
            out << (i ? "," : "") << "{\"at\":" << route_.cumulative_distance_m[m.point_index] << ",\"type\":\""
                << maneuver_name(m.type) << "\",\"street\":\"" << json_escape(m.street_name) << "\"}";
        }
        out << "]}";
        return out.str();
    }

    std::filesystem::path data_dir_;
    MapDataset demo_map_;
    MapDataset map_;
    Route route_;
    NavigationEngine navigation_;
    GpsFilter filter_;
    ColorMapRenderer renderer_;
    Canvas565 canvas_;
    NavigationSnapshot latest_;
    float progress_m_{};
    float speed_mps_{kDemoSpeedMps};
    bool auto_follow_{};
    bool phone_connected_{true};
    bool gps_available_{true};
    bool forced_off_route_{};
    bool noise_{true};
    bool smooth_{true};
    bool route_with_map_{true};
    bool have_gps_time_{};
    bool force_step_{};
    ThemeMode theme_{ThemeMode::automatic};
    ClockMode clock_mode_{ClockMode::live};
    std::int64_t clock_base_ms_{};
    std::chrono::steady_clock::time_point clock_set_at_{};
    std::pair<float, float> off_route_event_{};
    std::uint64_t last_advance_ms_{};
    std::uint64_t last_step_ms_{};
    std::uint32_t steps_{};
    std::uint32_t noise_state_{0xB14E05U};
};

void reply_json(const std::string& json)
{
    std::cout << "JSON " << json.size() << '\n' << json;
    std::cout.flush();
}

}  // namespace

int main(int argc, char** argv)
{
    std::ios::sync_with_stdio(false);
    const std::filesystem::path data_dir = argc > 1 ? argv[1] : BIKEOS_SOURCE_DIR "/data";
    try {
        Bench bench(data_dir);
        std::string line;
        std::vector<std::uint8_t> pixels;
        while (std::getline(std::cin, line)) {
            std::istringstream words(line);
            std::string verb;
            words >> verb;
            std::string error;
            std::string json;
            if (verb == "frame") {
                std::uint64_t now = 0;
                words >> now;
                json = bench.frame(now, pixels);
                std::cout << "FRAME " << json.size() << ' ' << pixels.size() << '\n' << json;
                std::cout.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
                std::cout.flush();
                continue;
            }
            try {
                if (verb == "set") {
                    std::string key, value;
                    words >> key >> value;
                    json = bench.set(key, value, error);
                } else if (verb == "seek") {
                    float metres = 0.0F;
                    words >> metres;
                    bench.seek(metres);
                    json = "{\"ok\":true}";
                } else if (verb == "command") {
                    std::string name;
                    words >> name;
                    json = bench.command(name, error);
                } else if (verb == "install") {
                    int with_map = 1;
                    std::string path;
                    words >> with_map;
                    std::getline(words >> std::ws, path);
                    if (bench.install(path, with_map != 0, error)) json = "{\"ok\":true}";
                } else if (verb == "map") {
                    json = bench.map_json();
                } else {
                    error = "unknown verb " + verb;
                }
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            reply_json(error.empty() ? json : "{\"error\":\"" + json_escape(error) + "\"}");
        }
    } catch (const std::exception& exception) {
        std::cerr << "virtual screen engine: " << exception.what() << '\n';
        return 1;
    }
    return 0;
}
