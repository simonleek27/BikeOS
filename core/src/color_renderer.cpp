#include "bikeos/color_renderer.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "bikeos/fonts.hpp"

namespace bikeos {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegreesToRadians = kPi / 180.0;
constexpr std::int64_t kDayMs = 86400000;
constexpr std::int64_t kHourMs = 3600000;
constexpr float kCyclingSpeedMps = 15.0F / 3.6F;  // ETA assumption from the spec

struct Palette {
    std::uint16_t land, water, park;
    std::uint16_t road_minor, road_major, road_casing, road_major_casing, cycleway;
    std::uint16_t route, route_ridden, rider, rider_outline;
    std::uint16_t label, water_label, label_halo;
    std::uint16_t card, card_border, text, subtext, muted, icon, icon_muted;
    std::uint16_t amber_card, amber_border, amber_text;
    std::uint16_t red_card, red_border, red_text;
    std::uint16_t gps_ok, gps_lost, gps_none, phone_on, phone_off;
};

constexpr Palette kDay{
    rgb565(0xE6, 0xE2, 0xDA), rgb565(0x9F, 0xCB, 0xEE), rgb565(0xC4, 0xE3, 0xA8),
    rgb565(0xFF, 0xFF, 0xFF), rgb565(0xFB, 0xD3, 0x8D), rgb565(0xD6, 0xD1, 0xC8), rgb565(0xD9, 0xA9, 0x5B),
    rgb565(0x16, 0x85, 0x7A),
    rgb565(0xD6, 0x00, 0x6F), rgb565(0xB9, 0xB5, 0xAE), rgb565(0x1E, 0x3A, 0x8A), rgb565(0xFF, 0xFF, 0xFF),
    rgb565(0x4B, 0x55, 0x63), rgb565(0x2B, 0x6A, 0xA8), rgb565(0xF7, 0xF5, 0xF1),
    rgb565(0xFF, 0xFF, 0xFF), rgb565(0xD6, 0xD2, 0xCA), rgb565(0x11, 0x18, 0x27), rgb565(0x37, 0x41, 0x51),
    rgb565(0x6B, 0x72, 0x80), rgb565(0x1F, 0x29, 0x37), rgb565(0xA8, 0xAD, 0xB5),
    rgb565(0xFD, 0xE7, 0xB0), rgb565(0xE0, 0xA5, 0x26), rgb565(0x5C, 0x3B, 0x00),
    rgb565(0xFB, 0xD0, 0xD0), rgb565(0xD9, 0x53, 0x4F), rgb565(0x6B, 0x11, 0x11),
    rgb565(0x16, 0xA3, 0x4A), rgb565(0xDC, 0x26, 0x26), rgb565(0x9C, 0xA3, 0xAF),
    rgb565(0x25, 0x63, 0xEB), rgb565(0x9C, 0xA3, 0xAF),
};

constexpr Palette kNight{
    rgb565(0x12, 0x14, 0x17), rgb565(0x1B, 0x33, 0x50), rgb565(0x1C, 0x33, 0x22),
    rgb565(0x3A, 0x40, 0x48), rgb565(0x5C, 0x66, 0x72), rgb565(0x12, 0x14, 0x17), rgb565(0x12, 0x14, 0x17),
    rgb565(0x26, 0xA6, 0x9A),
    rgb565(0xFF, 0x3F, 0xA4), rgb565(0x5A, 0x5F, 0x66), rgb565(0xFF, 0xFF, 0xFF), rgb565(0x12, 0x14, 0x17),
    rgb565(0xA6, 0xAF, 0xB9), rgb565(0x7F, 0xAE, 0xDD), rgb565(0x12, 0x14, 0x17),
    rgb565(0x1F, 0x24, 0x2B), rgb565(0x34, 0x3B, 0x44), rgb565(0xFF, 0xFF, 0xFF), rgb565(0xC9, 0xD1, 0xD9),
    rgb565(0x8B, 0x95, 0xA1), rgb565(0xFF, 0xFF, 0xFF), rgb565(0x59, 0x62, 0x6D),
    rgb565(0x4A, 0x33, 0x00), rgb565(0xB7, 0x79, 0x1F), rgb565(0xFF, 0xD2, 0x7A),
    rgb565(0x4A, 0x14, 0x14), rgb565(0xC5, 0x30, 0x30), rgb565(0xFF, 0xB4, 0xB4),
    rgb565(0x4A, 0xDE, 0x80), rgb565(0xF8, 0x71, 0x71), rgb565(0x59, 0x62, 0x6D),
    rgb565(0x60, 0xA5, 0xFA), rgb565(0x59, 0x62, 0x6D),
};

std::int64_t floor_div(const std::int64_t value, const std::int64_t divisor)
{
    return value / divisor - ((value % divisor != 0 && ((value < 0) != (divisor < 0))) ? 1 : 0);
}

std::int64_t days_from_civil(std::int64_t year, const unsigned month, const unsigned day)
{
    year -= month <= 2 ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const auto year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + static_cast<std::int64_t>(day_of_era) - 719468;
}

std::int64_t civil_year(const std::int64_t days)
{
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto day_of_era = static_cast<unsigned>(z - era * 146097);
    const unsigned year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const unsigned month_index = (5 * day_of_year + 2) / 153;
    const unsigned month = month_index < 10 ? month_index + 3 : month_index - 9;
    return static_cast<std::int64_t>(year_of_era) + era * 400 + (month <= 2 ? 1 : 0);
}

std::int64_t last_sunday(const std::int64_t year, const unsigned month, const unsigned last_day)
{
    const std::int64_t day = days_from_civil(year, month, last_day);
    const std::int64_t weekday = ((day + 4) % 7 + 7) % 7;  // 0 = Sunday; 1970-01-01 was a Thursday
    return day - weekday;
}

std::string clock_text(const std::int64_t local_ms)
{
    const std::int64_t minutes = floor_div(local_ms, 60000);
    const auto minute_of_day = static_cast<int>(((minutes % 1440) + 1440) % 1440);
    char text[8]{};
    std::snprintf(text, sizeof(text), "%02d:%02d", minute_of_day / 60, minute_of_day % 60);
    return text;
}

bool is_placeholder_name(const std::string_view name)
{
    static constexpr std::array<std::string_view, 15> kPlaceholders{
        "motorway", "trunk", "primary", "secondary", "tertiary", "unclassified", "residential",
        "living_street", "service", "cycleway", "path", "footway", "pedestrian", "steps", "",
    };
    return std::find(kPlaceholders.begin(), kPlaceholders.end(), name) != kPlaceholders.end();
}

std::pair<std::string, std::string> split_distance(const float metres)
{
    const float clamped = std::max(0.0F, metres);
    char number[16]{};
    if (clamped < 995.0F) {
        // Round to 10 m above 100 m; GPS is not better than that and the digits jump less.
        const long value = clamped < 100.0F ? std::lround(clamped) : std::lround(clamped / 10.0F) * 10;
        std::snprintf(number, sizeof(number), "%ld", value);
        return {number, "m"};
    }
    if (clamped < 9950.0F) {
        std::snprintf(number, sizeof(number), "%.1f", static_cast<double>(clamped / 1000.0F));
    } else {
        std::snprintf(number, sizeof(number), "%ld", std::lround(clamped / 1000.0F));
    }
    return {number, "km"};
}

const char* maneuver_phrase(const ManeuverType type)
{
    switch (type) {
    case ManeuverType::left: return "left";
    case ManeuverType::right: return "right";
    case ManeuverType::slight_left: return "slight left";
    case ManeuverType::slight_right: return "slight right";
    case ManeuverType::sharp_left: return "sharp left";
    case ManeuverType::sharp_right: return "sharp right";
    case ManeuverType::u_turn: return "U-turn";
    case ManeuverType::roundabout: return "roundabout";
    case ManeuverType::destination: return "arrive";
    case ManeuverType::straight: break;
    }
    return "straight on";
}

void fill_triangle(Canvas565& canvas, const PointF a, const PointF b, const PointF c, const std::uint16_t color)
{
    const std::array<PointF, 3> points{a, b, c};
    canvas.fill_polygon(points, color);
}

// Arrowhead whose tip is 10 px past `from` along (dx, dy).
void arrowhead(Canvas565& canvas, const PointF from, const float dx, const float dy, const float size,
               const std::uint16_t color)
{
    const PointF tip{from.x + dx * size, from.y + dy * size};
    const PointF base{from.x - dx * size * 0.1F, from.y - dy * size * 0.1F};
    const float half = size * 0.9F;
    fill_triangle(canvas, tip, {base.x - dy * half, base.y + dx * half}, {base.x + dy * half, base.y - dx * half}, color);
}

void draw_flag(Canvas565& canvas, const float x, const float y, const float s, const std::uint16_t color,
               const std::uint16_t background)
{
    canvas.thick_line({x + s * 0.28F, y + s * 0.92F}, {x + s * 0.28F, y + s * 0.08F}, s * 0.07F, color);
    const int left = static_cast<int>(std::lround(x + s * 0.32F));
    const int top = static_cast<int>(std::lround(y + s * 0.1F));
    const int cell = std::max(2, static_cast<int>(std::lround(s * 0.13F)));
    canvas.fill_rect(left, top, cell * 4, cell * 3, background);
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            if ((row + column) % 2 == 0) canvas.fill_rect(left + column * cell, top + row * cell, cell, cell, color);
        }
    }
    canvas.stroke_round_rect(left, top, cell * 4, cell * 3, 0, color);
}

// Turn icon filling an s x s box whose top-left corner is (x, y).
void draw_turn_icon(Canvas565& canvas, const float x, const float y, const float s, const ManeuverType type,
                    const std::uint16_t color, const std::uint16_t background)
{
    const float shaft = s * 0.14F;
    const auto at = [&](const float u, const float v) { return PointF{x + u * s, y + v * s}; };
    if (type == ManeuverType::destination) {
        draw_flag(canvas, x, y, s, color, background);
        return;
    }
    if (type == ManeuverType::u_turn) {
        std::vector<PointF> path{at(0.68F, 0.92F), at(0.68F, 0.40F)};
        for (int step = 0; step <= 12; ++step) {
            const float angle = static_cast<float>(kPi) * static_cast<float>(step) / 12.0F;
            path.push_back(at(0.50F + 0.18F * std::cos(angle), 0.40F - 0.18F * std::sin(angle)));
        }
        path.push_back(at(0.32F, 0.58F));
        canvas.polyline(path, shaft, color);
        arrowhead(canvas, at(0.32F, 0.60F), 0.0F, 1.0F, s * 0.2F, color);
        return;
    }
    if (type == ManeuverType::roundabout) {
        std::vector<PointF> ring;
        for (int step = 0; step <= 24; ++step) {
            const float angle = 2.0F * static_cast<float>(kPi) * static_cast<float>(step) / 24.0F;
            ring.push_back(at(0.5F + 0.2F * std::cos(angle), 0.52F + 0.2F * std::sin(angle)));
        }
        canvas.polyline(ring, shaft * 0.8F, color);
        canvas.thick_line(at(0.5F, 0.96F), at(0.5F, 0.72F), shaft, color);
        canvas.thick_line(at(0.5F, 0.32F), at(0.5F, 0.16F), shaft, color);
        arrowhead(canvas, at(0.5F, 0.16F), 0.0F, -1.0F, s * 0.2F, color);
        return;
    }
    float degrees = 0.0F;
    switch (type) {
    case ManeuverType::left: degrees = -90.0F; break;
    case ManeuverType::right: degrees = 90.0F; break;
    case ManeuverType::slight_left: degrees = -45.0F; break;
    case ManeuverType::slight_right: degrees = 45.0F; break;
    case ManeuverType::sharp_left: degrees = -135.0F; break;
    case ManeuverType::sharp_right: degrees = 135.0F; break;
    default: break;
    }
    const float radians = degrees * static_cast<float>(kDegreesToRadians);
    const float dx = std::sin(radians);
    const float dy = -std::cos(radians);
    if (degrees == 0.0F) {
        canvas.thick_line(at(0.5F, 0.94F), at(0.5F, 0.30F), shaft, color);
        arrowhead(canvas, at(0.5F, 0.30F), 0.0F, -1.0F, s * 0.22F, color);
        return;
    }
    // Shift the shaft away from the turn so the arm and head stay in the box.
    const float shaft_u = 0.5F - dx * 0.16F;
    const float elbow_v = std::abs(degrees) > 100.0F ? 0.34F : 0.50F;
    const PointF elbow = at(shaft_u, elbow_v);
    const float arm = s * (std::abs(degrees) > 100.0F ? 0.22F : 0.24F);
    const PointF end{elbow.x + dx * arm, elbow.y + dy * arm};
    canvas.polyline(std::array<PointF, 3>{at(shaft_u, 0.96F), elbow, end}, shaft, color);
    arrowhead(canvas, end, dx, dy, s * 0.22F, color);
}

void draw_gps_icon(Canvas565& canvas, const float x, const float y, const float s, const std::uint16_t color)
{
    std::vector<PointF> ring;
    for (int step = 0; step <= 24; ++step) {
        const float angle = 2.0F * static_cast<float>(kPi) * static_cast<float>(step) / 24.0F;
        ring.push_back({x + s * (0.5F + 0.3F * std::cos(angle)), y + s * (0.5F + 0.3F * std::sin(angle))});
    }
    canvas.polyline(ring, s * 0.09F, color);
    canvas.fill_circle({x + s * 0.5F, y + s * 0.5F}, s * 0.1F, color);
    for (const auto [ax, ay, bx, by] : std::array<std::array<float, 4>, 4>{{
             {0.5F, 0.04F, 0.5F, 0.2F}, {0.5F, 0.8F, 0.5F, 0.96F}, {0.04F, 0.5F, 0.2F, 0.5F}, {0.8F, 0.5F, 0.96F, 0.5F}}}) {
        canvas.thick_line({x + s * ax, y + s * ay}, {x + s * bx, y + s * by}, s * 0.08F, color);
    }
}

void draw_pin_icon(Canvas565& canvas, const float x, const float y, const float s, const std::uint16_t color,
                   const std::uint16_t background)
{
    const PointF centre{x + s * 0.5F, y + s * 0.38F};
    fill_triangle(canvas, {centre.x - s * 0.26F, centre.y + s * 0.12F}, {centre.x + s * 0.26F, centre.y + s * 0.12F},
                  {centre.x, y + s * 0.96F}, color);
    canvas.fill_circle(centre, s * 0.3F, color);
    canvas.fill_circle(centre, s * 0.11F, background);
}

void draw_phone_glyph(Canvas565& canvas, const int x, const int y, const std::uint16_t color)
{
    canvas.stroke_round_rect(x, y, 9, 14, 2, color);
    canvas.fill_rect(x + 3, y + 11, 3, 1, color);
}

// Greedy word wrap into at most two lines; the second is ellipsised if needed.
std::vector<std::string> wrap_text(const Font& font, const std::string_view text, const int width)
{
    std::vector<std::string> lines;
    if (text.empty()) return lines;
    if (text_width(font, text) <= width) return {std::string(text)};
    std::size_t split = std::string_view::npos;
    for (std::size_t space = text.find(' '); space != std::string_view::npos; space = text.find(' ', space + 1)) {
        if (text_width(font, text.substr(0, space)) > width) break;
        split = space;
    }
    if (split == std::string_view::npos) return {fit_text(font, text, width)};
    lines.emplace_back(text.substr(0, split));
    lines.push_back(fit_text(font, text.substr(split + 1), width));
    return lines;
}

struct CardStyle {
    std::uint16_t background, border, title, subtitle, icon;
};

}  // namespace

std::int64_t amsterdam_local_ms(const std::int64_t utc_epoch_ms)
{
    const std::int64_t year = civil_year(floor_div(utc_epoch_ms, kDayMs));
    const std::int64_t summer_start = last_sunday(year, 3, 31) * kDayMs + kHourMs;
    const std::int64_t summer_end = last_sunday(year, 10, 31) * kDayMs + kHourMs;
    const bool summer = utc_epoch_ms >= summer_start && utc_epoch_ms < summer_end;
    return utc_epoch_ms + (summer ? 2 : 1) * kHourMs;
}

double solar_elevation_deg(const std::int64_t utc_epoch_ms, const GeoPointE7 position)
{
    const double n = static_cast<double>(utc_epoch_ms) / static_cast<double>(kDayMs) + 2440587.5 - 2451545.0;
    const double mean_longitude = std::fmod(280.460 + 0.9856474 * n, 360.0);
    const double anomaly = (357.528 + 0.9856003 * n) * kDegreesToRadians;
    const double ecliptic = (mean_longitude + 1.915 * std::sin(anomaly) + 0.020 * std::sin(2.0 * anomaly)) *
                            kDegreesToRadians;
    const double obliquity = (23.439 - 0.0000004 * n) * kDegreesToRadians;
    const double declination = std::asin(std::sin(obliquity) * std::sin(ecliptic));
    const double right_ascension = std::atan2(std::cos(obliquity) * std::sin(ecliptic), std::cos(ecliptic));
    const double sidereal_hours = std::fmod(18.697374558 + 24.06570982441908 * n, 24.0);
    const double longitude = position.longitude_e7 * 1.0e-7;
    const double latitude = position.latitude_e7 * 1.0e-7 * kDegreesToRadians;
    const double hour_angle = (sidereal_hours * 15.0 + longitude) * kDegreesToRadians - right_ascension;
    const double sine = std::sin(latitude) * std::sin(declination) +
                        std::cos(latitude) * std::cos(declination) * std::cos(hour_angle);
    return std::asin(std::clamp(sine, -1.0, 1.0)) / kDegreesToRadians;
}

bool night_theme(const ColorFrameInput& input, const GeoPointE7 position)
{
    if (input.theme == ThemeMode::day) return false;
    if (input.theme == ThemeMode::night) return true;
    if (input.utc_epoch_ms == 0 || !valid_coordinate(position)) return true;
    return solar_elevation_deg(input.utc_epoch_ms, position) < -2.0;
}

std::string format_distance(const float metres)
{
    const auto [number, unit] = split_distance(metres);
    return number + " " + unit;
}

namespace {

constexpr double kMetresPerE7 = 6371008.8 * kDegreesToRadians * 1.0e-7;

template <typename Points>
void extend_bounds(const Points& points, std::int64_t& min_lat, std::int64_t& max_lat, std::int64_t& min_lon,
                   std::int64_t& max_lon)
{
    for (const auto point : points) {
        min_lat = std::min<std::int64_t>(min_lat, point.latitude_e7);
        max_lat = std::max<std::int64_t>(max_lat, point.latitude_e7);
        min_lon = std::min<std::int64_t>(min_lon, point.longitude_e7);
        max_lon = std::max<std::int64_t>(max_lon, point.longitude_e7);
    }
}

}  // namespace

void ColorMapRenderer::update_bounds(const MapDataset& map)
{
    if (bounds_map_ == &map && bounds_roads_ == map.roads.size() && bounds_areas_ == map.areas.size()) return;
    const auto circle = [](const std::int64_t min_lat, const std::int64_t max_lat, const std::int64_t min_lon,
                           const std::int64_t max_lon) {
        const GeoPointE7 centre{static_cast<std::int32_t>((min_lat + max_lat) / 2),
                                static_cast<std::int32_t>((min_lon + max_lon) / 2)};
        const double north = static_cast<double>(max_lat - min_lat) * 0.5 * kMetresPerE7;
        const double east = static_cast<double>(max_lon - min_lon) * 0.5 * kMetresPerE7 *
                            std::cos(centre.latitude_e7 * 1.0e-7 * kDegreesToRadians);
        return Bounds{centre, static_cast<float>(std::hypot(north, east))};
    };
    road_bounds_.clear();
    road_bounds_.reserve(map.roads.size());
    for (const auto& road : map.roads) {
        std::int64_t min_lat = INT32_MAX, max_lat = INT32_MIN, min_lon = INT32_MAX, max_lon = INT32_MIN;
        extend_bounds(road.points, min_lat, max_lat, min_lon, max_lon);
        road_bounds_.push_back(circle(min_lat, max_lat, min_lon, max_lon));
    }
    area_bounds_.clear();
    area_bounds_.reserve(map.areas.size());
    for (const auto& area : map.areas) {
        std::int64_t min_lat = INT32_MAX, max_lat = INT32_MIN, min_lon = INT32_MAX, max_lon = INT32_MIN;
        extend_bounds(area.rings.front(), min_lat, max_lat, min_lon, max_lon);
        area_bounds_.push_back(circle(min_lat, max_lat, min_lon, max_lon));
    }
    bounds_map_ = &map;
    bounds_roads_ = map.roads.size();
    bounds_areas_ = map.areas.size();
}

bool ColorMapRenderer::Rect::intersects(const Rect& other) const
{
    return x < other.x + other.width && other.x < x + width && y < other.y + other.height && other.y < y + height;
}

void ColorMapRenderer::render(Canvas565& canvas,
                              const MapDataset& map,
                              const std::span<const GeoPointE7> route,
                              const NavigationSnapshot& navigation,
                              const ColorFrameInput& input)
{
    const int width = canvas.width();
    const int height = canvas.height();
    const NavigationState state = navigation.state;
    const bool has_route = state != NavigationState::no_route && route.size() >= 2;

    // Where the rider is, or best guess.
    GeoPointE7 target_position{};
    bool target_known = false;
    if (navigation.gps_valid && valid_coordinate(navigation.position)) {
        target_position = navigation.position;
        target_known = true;
    } else if (have_position_) {
        target_position = to_position_;
        target_known = true;
    } else if (navigation.route_progress_m > 0.0F && valid_coordinate(navigation.position)) {
        // A renderer that started after the fix was lost still knows where the engine last saw the rider.
        target_position = navigation.position;
        target_known = true;
    }
    if (navigation.heading_valid) {
        to_heading_deg_ = navigation.heading_deg;
        if (!have_heading_) from_heading_deg_ = to_heading_deg_;
        have_heading_ = true;
    }

    // Glide between fixes: each new fix starts a move from wherever the rider
    // is drawn now to the new fix, lasting about one fix interval. This keeps
    // the map moving smoothly at the frame rate instead of jumping once a
    // second, at the cost of drawing the rider ~1 s behind the latest fix.
    const std::uint64_t now = input.now_ms;
    const auto shown_fraction = [&] {
        if (now == 0 || move_duration_ms_ == 0) return 1.0;
        return std::clamp(static_cast<double>(now - move_start_ms_) / move_duration_ms_, 0.0, 1.0);
    };
    const auto lerp_position = [](const GeoPointE7 a, const GeoPointE7 b, const double t) {
        return GeoPointE7{
            static_cast<std::int32_t>(std::llround(a.latitude_e7 + (b.latitude_e7 - a.latitude_e7) * t)),
            static_cast<std::int32_t>(std::llround(a.longitude_e7 + (b.longitude_e7 - a.longitude_e7) * t)),
        };
    };
    const auto lerp_heading = [](const float a, const float b, const double t) {
        const float delta = std::remainder(b - a, 360.0F);  // shortest way round
        return static_cast<float>(normalize_heading_deg(a + delta * t));
    };
    if (target_known) {
        if (!have_position_) {
            from_position_ = to_position_ = target_position;
            have_position_ = true;
        } else if (!(target_position == to_position_) || to_heading_deg_ != move_heading_target_deg_) {
            const double t = shown_fraction();
            const GeoPointE7 shown = lerp_position(from_position_, to_position_, t);
            from_heading_deg_ = lerp_heading(from_heading_deg_, move_heading_target_deg_, t);
            // Snap instead of gliding across a jump (reroute, GPS reacquired far away).
            from_position_ = distance_m(shown, target_position) > 60.0 ? target_position : shown;
            to_position_ = target_position;
            const std::uint64_t interval = last_fix_ms_ == 0 ? 1000 : now - last_fix_ms_;
            move_duration_ms_ = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(interval, 250, 1500));
            move_start_ms_ = now;
            last_fix_ms_ = now;
        }
        move_heading_target_deg_ = to_heading_deg_;
    }
    const double moved = shown_fraction();
    const GeoPointE7 shown_position = have_position_ ? lerp_position(from_position_, to_position_, moved) : GeoPointE7{};
    const float shown_heading = have_heading_ ? lerp_heading(from_heading_deg_, move_heading_target_deg_, moved) : 0.0F;
    GeoPointE7 centre = map.origin;
    if (have_position_) centre = shown_position;
    else if (has_route) centre = route.front();

    const bool night = night_theme(input, centre);
    const Palette& p = night ? kNight : kDay;

    // Zoom: fixed while cruising, closer in the last stretch before a turn.
    const bool guiding = state == NavigationState::navigating || state == NavigationState::phone_disconnected;
    const float target = guiding && navigation.next_maneuver &&
                                 navigation.distance_to_maneuver_m <= turn_zoom_distance_m
                             ? turn_meters_per_pixel
                             : cruise_meters_per_pixel;
    if (!zoom_initialized_) {
        meters_per_pixel_ = target;
        zoom_initialized_ = true;
    }
    // Ease towards the target zoom with a ~0.7 s time constant at any frame rate.
    const float zoom_step = now == 0 || last_frame_ms_ == 0
                                ? 0.35F
                                : 1.0F - std::exp(-static_cast<float>(now - last_frame_ms_) / 700.0F);
    last_frame_ms_ = now;
    meters_per_pixel_ += (target - meters_per_pixel_) * std::clamp(zoom_step, 0.0F, 1.0F);
    const float mpp = meters_per_pixel_;
    const float line_scale = std::clamp(cruise_meters_per_pixel / mpp, 1.0F, 1.8F);

    const double heading = have_heading_ ? shown_heading * kDegreesToRadians : 0.0;
    const double cos_h = std::cos(heading);
    const double sin_h = std::sin(heading);
    // Flat projection around the rider; at a few hundred metres the error is
    // far below a pixel, and it avoids trig per point.
    const double north_per_e7 = kMetresPerE7;
    const double east_per_e7 = kMetresPerE7 * std::cos(centre.latitude_e7 * 1.0e-7 * kDegreesToRadians);
    const auto local = [&](const GeoPointE7 point) {
        return std::pair<double, double>{
            static_cast<double>(point.longitude_e7 - centre.longitude_e7) * east_per_e7,
            static_cast<double>(point.latitude_e7 - centre.latitude_e7) * north_per_e7};
    };
    const double inverse_mpp = 1.0 / mpp;
    const auto to_screen = [&](const GeoPointE7 point) {
        const auto [local_east, local_north] = local(point);
        // Rotate by +heading so the direction of travel points up the screen.
        const double east = local_east * cos_h - local_north * sin_h;
        const double north = local_east * sin_h + local_north * cos_h;
        return PointF{static_cast<float>(rider_x + east * inverse_mpp), static_cast<float>(rider_y - north * inverse_mpp)};
    };
    // Farthest screen corner from the rider, plus a margin for line widths.
    update_bounds(map);
    const double view_radius_m = (std::hypot(std::max(rider_x, width - rider_x), std::max(rider_y, height - rider_y)) + 16.0) * mpp;
    const auto in_view = [&](const Bounds& bounds) {
        const auto [east, north] = local(bounds.centre);
        return std::hypot(east, north) - bounds.radius_m <= view_radius_m;
    };
    const auto visible = [&](const std::vector<PointF>& points, const float margin) {
        float min_x = 1e9F, min_y = 1e9F, max_x = -1e9F, max_y = -1e9F;
        for (const auto point : points) {
            min_x = std::min(min_x, point.x);
            max_x = std::max(max_x, point.x);
            min_y = std::min(min_y, point.y);
            max_y = std::max(max_y, point.y);
        }
        return max_x >= -margin && min_x <= width + margin && max_y >= -margin && min_y <= height + margin;
    };

    canvas.clear(p.land);

    // Areas: parks under water so ponds in parks show.
    std::vector<std::vector<PointF>> rings;
    std::vector<std::span<const PointF>> ring_views;
    for (const AreaClass layer : {AreaClass::park, AreaClass::water}) {
        for (std::size_t index = 0; index < map.areas.size(); ++index) {
            const auto& area = map.areas[index];
            if (area.area_class != layer || !in_view(area_bounds_[index])) continue;
            rings.clear();
            for (const auto& ring : area.rings) {
                auto& screen = rings.emplace_back();
                screen.reserve(ring.size());
                for (const auto point : ring) screen.push_back(to_screen(point));
            }
            if (rings.empty() || !visible(rings.front(), 0.0F)) continue;
            ring_views.assign(rings.begin(), rings.end());
            canvas.fill_polygon(ring_views, layer == AreaClass::water ? p.water : p.park);
        }
    }

    // Roads: casings, then minor fills, then major fills, then bike paths.
    struct ScreenRoad {
        const RoadPolyline* road;
        std::vector<PointF> points;
    };
    std::vector<ScreenRoad> roads;
    roads.reserve(map.roads.size() / 2);
    for (std::size_t index = 0; index < map.roads.size(); ++index) {
        const auto& road = map.roads[index];
        // Footpaths and service alleys add clutter without helping a cyclist navigate.
        if (road.road_class == RoadClass::footway || road.road_class == RoadClass::service ||
            road.road_class == RoadClass::other) {
            continue;
        }
        if (!in_view(road_bounds_[index])) continue;
        ScreenRoad screen{&road, {}};
        screen.points.reserve(road.points.size());
        for (const auto point : road.points) screen.points.push_back(to_screen(point));
        if (visible(screen.points, 12.0F)) roads.push_back(std::move(screen));
    }
    const auto is_major = [](const RoadClass road_class) {
        return road_class == RoadClass::motorway || road_class == RoadClass::primary ||
               road_class == RoadClass::secondary;
    };
    const auto road_width = [&](const RoadClass road_class) {
        switch (road_class) {
        case RoadClass::motorway: return 7.5F * line_scale;
        case RoadClass::primary: return 6.5F * line_scale;
        case RoadClass::secondary: return 6.0F * line_scale;
        case RoadClass::residential: return 3.6F * line_scale;
        case RoadClass::service: return 2.2F * line_scale;
        case RoadClass::cycleway: return 2.4F * line_scale;
        default: return 2.0F * line_scale;
        }
    };
    for (const bool major : {false, true}) {
        for (const auto& road : roads) {
            const RoadClass road_class = road.road->road_class;
            if (road_class == RoadClass::cycleway || is_major(road_class) != major) continue;
            canvas.polyline(road.points, road_width(road_class), major ? p.road_major : p.road_minor);
        }
    }
    for (const auto& road : roads) {
        if (road.road->road_class == RoadClass::cycleway) {
            canvas.polyline(road.points, road_width(RoadClass::cycleway), p.cycleway);
        }
    }

    // Route: gray behind the rider, magenta ahead.
    if (has_route) {
        std::vector<PointF> ridden;
        std::vector<PointF> ahead;
        const bool started = (navigation.gps_valid || navigation.route_progress_m > 0.0F) &&
                             valid_coordinate(navigation.matched_position) &&
                             navigation.matched_segment + 1 < route.size();
        if (started) {
            // Split where the gliding rider is drawn, not at the last fix, so
            // the magenta line always starts at the arrow.
            const PointF rider{static_cast<float>(rider_x), static_cast<float>(rider_y)};
            std::size_t split_segment = navigation.matched_segment;
            PointF split = to_screen(navigation.matched_position);
            float best = std::numeric_limits<float>::max();
            const std::size_t first = navigation.matched_segment > 2 ? navigation.matched_segment - 2 : 0;
            const std::size_t last = std::min(route.size() - 2, navigation.matched_segment + 2);
            for (std::size_t i = first; i <= last; ++i) {
                const PointF a = to_screen(route[i]);
                const PointF b = to_screen(route[i + 1]);
                const float dx = b.x - a.x;
                const float dy = b.y - a.y;
                const float length_sq = dx * dx + dy * dy;
                const float t = length_sq > 0.0F
                                    ? std::clamp(((rider.x - a.x) * dx + (rider.y - a.y) * dy) / length_sq, 0.0F, 1.0F)
                                    : 0.0F;
                const PointF candidate{a.x + dx * t, a.y + dy * t};
                const float distance = std::hypot(candidate.x - rider.x, candidate.y - rider.y);
                if (distance < best) {
                    best = distance;
                    split = candidate;
                    split_segment = i;
                }
            }
            for (std::size_t i = 0; i <= split_segment; ++i) ridden.push_back(to_screen(route[i]));
            ridden.push_back(split);
            ahead.push_back(split);
            for (std::size_t i = split_segment + 1; i < route.size(); ++i) ahead.push_back(to_screen(route[i]));
        } else {
            for (const auto point : route) ahead.push_back(to_screen(point));
        }
        const float route_width = 6.5F * std::min(line_scale, 1.4F);
        canvas.polyline(ridden, route_width - 1.0F, p.route_ridden);
        canvas.polyline(ahead, route_width, p.route);
        const PointF destination = to_screen(route.back());
        canvas.fill_circle(destination, 7.0F, p.rider_outline);
        canvas.fill_circle(destination, 5.0F, p.route);
        canvas.fill_circle(destination, 2.0F, p.rider_outline);
    }

    // Card content, computed before labels so labels avoid it.
    struct Card {
        CardStyle style;
        bool maneuver_layout{};
        enum class Icon { maneuver, flag, gps, pin } icon{Icon::pin};
        ManeuverType maneuver{ManeuverType::straight};
        std::string distance_number, distance_unit, title, subtitle, then_line;
    } card;
    const CardStyle neutral{p.card, p.card_border, p.text, p.subtext, p.icon};
    const CardStyle amber{p.amber_card, p.amber_border, p.amber_text, p.amber_text, p.icon_muted};
    const CardStyle red{p.red_card, p.red_border, p.red_text, p.red_text, p.icon_muted};
    card.style = neutral;
    const auto maneuver_icon = [&](const Card::Icon fallback) {
        if (navigation.next_maneuver) {
            card.maneuver = navigation.next_maneuver->type;
            card.icon = card.maneuver == ManeuverType::destination ? Card::Icon::flag : Card::Icon::maneuver;
        } else {
            card.icon = fallback;
        }
    };
    switch (state) {
    case NavigationState::navigating:
    case NavigationState::phone_disconnected:
        if (navigation.next_maneuver) {
            card.maneuver_layout = true;
            maneuver_icon(Card::Icon::pin);
            std::tie(card.distance_number, card.distance_unit) = split_distance(navigation.distance_to_maneuver_m);
            const auto& street = navigation.next_maneuver->street_name;
            card.title = !street.empty() && !is_placeholder_name(street) ? street
                         : card.maneuver == ManeuverType::destination ? "Destination"
                                                                      : maneuver_phrase(card.maneuver);
            if (navigation.following_maneuver && navigation.following_maneuver_gap_m <= then_line_max_gap_m) {
                card.then_line = std::string("then ") + maneuver_phrase(navigation.following_maneuver->type) +
                                 " in " + format_distance(navigation.following_maneuver_gap_m);
            }
        } else {
            card.icon = Card::Icon::pin;
            card.title = "On route";
        }
        break;
    case NavigationState::off_route:
    case NavigationState::rerouting:
        card.style = amber;
        maneuver_icon(Card::Icon::gps);
        card.title = state == NavigationState::rerouting ? "Rerouting\xE2\x80\xA6" : "Off route";
        card.subtitle = state == NavigationState::rerouting ? "Finding a new route" : "Head back to the route";
        break;
    case NavigationState::gps_lost:
        card.style = red;
        maneuver_icon(Card::Icon::gps);
        card.title = "GPS lost";
        card.subtitle = "Showing last position";
        break;
    case NavigationState::waiting_for_gps:
        card.icon = Card::Icon::gps;
        card.style.icon = p.icon_muted;
        card.title = "Waiting for GPS";
        card.subtitle = "Antenna facing the sky";
        break;
    case NavigationState::destination_reached: {
        card.icon = Card::Icon::flag;
        card.title = "Arrived";
        const std::string street = navigation.next_maneuver ? navigation.next_maneuver->street_name : "";
        card.subtitle = is_placeholder_name(street) ? "" : street;
        break;
    }
    case NavigationState::error:
        card.style = red;
        card.icon = Card::Icon::gps;
        card.title = "Map error";
        card.subtitle = "Restart BikeOS";
        break;
    case NavigationState::no_route:
        card.icon = Card::Icon::pin;
        card.title = "No route";
        card.subtitle = "Pick a destination on your phone";
        break;
    }

    constexpr int card_x = 6;
    constexpr int card_y = 6;
    constexpr int max_card_width = 204;  // leaves a gap before the clock chip
    const Font* title_font = &font_title;
    std::vector<std::string> subtitle_lines;
    int card_width = 150;
    int card_height = 0;
    if (card.maneuver_layout) {
        const int distance_width = text_width(font_distance, card.distance_number) + 4 +
                                   text_width(font_title, card.distance_unit);
        card_width = std::clamp(std::max(62 + distance_width + 12, text_width(font_title, card.title) + 24),
                                150, max_card_width);
        card_height = card.then_line.empty() ? 76 : 92;
    } else {
        constexpr int text_room = max_card_width - 52 - 10;
        if (text_width(font_heading, card.title) <= text_room) title_font = &font_heading;
        subtitle_lines = wrap_text(font_small, card.subtitle, text_room);
        int text = text_width(*title_font, card.title);
        for (const auto& line : subtitle_lines) text = std::max(text, text_width(font_small, line));
        card_width = std::clamp(52 + text + 12, 150, max_card_width);
        card_height = 10 + title_font->ascent + static_cast<int>(subtitle_lines.size()) * 15 + 10;
    }
    card_ = {card_x, card_y, card_width, card_height};

    // Chips, also before labels.
    const bool time_known = input.utc_epoch_ms != 0;
    const std::string clock = time_known ? clock_text(amsterdam_local_ms(input.utc_epoch_ms)) : "--:--";
    const int clock_width = text_width(font_title, clock);
    const int chip_width = 10 + clock_width + 10 + 8 + 7 + 9 + 10;
    const Rect chip{width - 6 - chip_width, 6, chip_width, 24};
    const bool show_eta = has_route && state != NavigationState::destination_reached &&
                          state != NavigationState::waiting_for_gps && navigation.route_remaining_m > 0.0F;
    std::string eta_text;
    if (show_eta) {
        if (time_known) {
            const auto seconds = static_cast<std::int64_t>(navigation.route_remaining_m / kCyclingSpeedMps);
            eta_text = "ETA " + clock_text(amsterdam_local_ms(input.utc_epoch_ms + seconds * 1000));
        } else {
            eta_text = format_distance(navigation.route_remaining_m) + " left";
        }
    }
    const int eta_width = show_eta ? text_width(font_small, eta_text) + 20 : 0;
    const Rect eta_chip{width - 6 - eta_width, 34, eta_width, 20};

    // Labels: major streets and named water, never under the card, chips or rider.
    std::vector<Rect> occupied{card_, chip, Rect{rider_x - 18, rider_y - 18, 36, 36}};
    if (show_eta) occupied.push_back(eta_chip);
    struct Candidate {
        std::string_view name;
        PointF at;
        float length;
        bool water;
    };
    std::vector<Candidate> candidates;
    for (const auto& road : roads) {
        if (!is_major(road.road->road_class) || is_placeholder_name(road.road->name)) continue;
        for (std::size_t i = 1; i < road.points.size(); ++i) {
            const PointF a = road.points[i - 1];
            const PointF b = road.points[i];
            const auto inside = [&](const PointF q) { return q.x > 8 && q.x < width - 8 && q.y > 8 && q.y < height - 8; };
            if (!inside(a) || !inside(b)) continue;
            const float length = std::hypot(b.x - a.x, b.y - a.y);
            candidates.push_back({road.road->name, {(a.x + b.x) * 0.5F, (a.y + b.y) * 0.5F}, length, false});
        }
    }
    for (const auto& area : map.areas) {
        if (area.area_class != AreaClass::water || area.name.empty()) continue;
        float sum_x = 0.0F, sum_y = 0.0F;
        int count = 0;
        for (const auto point : area.rings.front()) {
            const PointF screen = to_screen(point);
            if (screen.x > 0 && screen.x < width && screen.y > 0 && screen.y < height) {
                sum_x += screen.x;
                sum_y += screen.y;
                ++count;
            }
        }
        if (count < 3) continue;
        const PointF at{sum_x / count, sum_y / count};
        // Only label where the centroid actually lands on this water.
        if (canvas.pixel(static_cast<int>(at.x), static_cast<int>(at.y)) != p.water) continue;
        candidates.push_back({area.name, at, 60.0F + static_cast<float>(count), true});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.length > b.length; });
    std::vector<std::string_view> labelled;
    for (const auto& candidate : candidates) {
        if (labelled.size() >= 4) break;
        if (std::find(labelled.begin(), labelled.end(), candidate.name) != labelled.end()) continue;
        const int text = text_width(font_label, candidate.name);
        if (!candidate.water && candidate.length < text * 0.6F) continue;
        const Rect rect{static_cast<int>(candidate.at.x) - text / 2 - 2,
                        static_cast<int>(candidate.at.y) - font_label.ascent / 2 - 2, text + 4,
                        font_label.ascent + 4};
        if (rect.x < 2 || rect.y < 2 || rect.x + rect.width > width - 2 || rect.y + rect.height > height - 2) continue;
        if (std::any_of(occupied.begin(), occupied.end(), [&](const Rect& other) { return rect.intersects(other); })) {
            continue;
        }
        canvas.draw_text_halo(font_label, rect.x + 2, rect.y + 2 + font_label.ascent - 2, candidate.name,
                              candidate.water ? p.water_label : p.label, candidate.water ? p.water : p.label_halo);
        occupied.push_back(rect);
        labelled.push_back(candidate.name);
    }

    // Rider arrow.
    if (have_position_) {
        const PointF at = to_screen(shown_position);
        // Heading-up: the arrow always points up. Before the first heading the
        // map is north-up and the arrow still points up (direction unknown).
        const auto shape = [&](const float scale) {
            std::array<PointF, 4> points{PointF{0.0F, -12.0F}, PointF{8.5F, 9.0F}, PointF{0.0F, 4.0F},
                                         PointF{-8.5F, 9.0F}};
            for (auto& point : points) point = {at.x + point.x * scale, at.y + point.y * scale};
            return points;
        };
        const bool lost = state == NavigationState::gps_lost;
        canvas.fill_polygon(shape(1.35F), p.rider_outline);
        canvas.fill_polygon(shape(1.0F), lost ? p.icon_muted : p.rider);
    }

    // Turn card.
    canvas.fill_round_rect(card_.x, card_.y, card_.width, card_.height, 10, card.style.background);
    canvas.stroke_round_rect(card_.x, card_.y, card_.width, card_.height, 10, card.style.border);
    const float icon_size = card.maneuver_layout ? 44.0F : 36.0F;
    const float icon_x = static_cast<float>(card_.x + (card.maneuver_layout ? 10 : 8));
    const float icon_y = static_cast<float>(card_.y + (card.maneuver_layout ? 8 : (card_.height - 36) / 2));
    switch (card.icon) {
    case Card::Icon::maneuver:
        draw_turn_icon(canvas, icon_x, icon_y, icon_size, card.maneuver, card.style.icon, card.style.background);
        break;
    case Card::Icon::flag:
        draw_flag(canvas, icon_x, icon_y, icon_size, card.style.icon, card.style.background);
        break;
    case Card::Icon::gps:
        draw_gps_icon(canvas, icon_x, icon_y, icon_size, card.style.icon);
        break;
    case Card::Icon::pin:
        draw_pin_icon(canvas, icon_x, icon_y, icon_size, card.style.icon, card.style.background);
        break;
    }
    if (card.maneuver_layout) {
        const int number_x = card_.x + 62;
        const int baseline = card_.y + 42;
        const int number_width = canvas.draw_text(font_distance, number_x, baseline, card.distance_number,
                                                  card.style.title);
        canvas.draw_text(font_title, number_x + number_width + 4, baseline, card.distance_unit, card.style.subtitle);
        canvas.draw_text(font_title, card_.x + 12, card_.y + 68,
                         fit_text(font_title, card.title, card_.width - 24), card.style.title);
        if (!card.then_line.empty()) {
            canvas.draw_text(font_small, card_.x + 12, card_.y + 85,
                             fit_text(font_small, card.then_line, card_.width - 24), p.muted);
        }
    } else {
        const int text_x = card_.x + 52;
        const int text_room = card_.width - 52 - 10;
        const int title_baseline = card_.y + 8 + title_font->ascent;
        canvas.draw_text(*title_font, text_x, title_baseline, fit_text(*title_font, card.title, text_room),
                         card.style.title);
        for (std::size_t line = 0; line < subtitle_lines.size(); ++line) {
            canvas.draw_text(font_small, text_x, title_baseline + 17 + static_cast<int>(line) * 15,
                             subtitle_lines[line], card.style.subtitle);
        }
    }

    // Status chips.
    canvas.fill_round_rect(chip.x, chip.y, chip.width, chip.height, 12, p.card);
    canvas.stroke_round_rect(chip.x, chip.y, chip.width, chip.height, 12, p.card_border);
    canvas.draw_text(font_title, chip.x + 10, chip.y + 17, clock, time_known ? p.text : p.muted);
    const int dot_x = chip.x + 10 + clock_width + 10 + 4;
    const std::uint16_t gps_color = navigation.gps_valid                  ? p.gps_ok
                                    : state == NavigationState::gps_lost ? p.gps_lost
                                                                         : p.gps_none;
    canvas.fill_circle({static_cast<float>(dot_x), static_cast<float>(chip.y + 12)}, 4.0F, gps_color);
    draw_phone_glyph(canvas, dot_x + 4 + 7, chip.y + 5, navigation.phone_connected ? p.phone_on : p.phone_off);
    if (show_eta) {
        canvas.fill_round_rect(eta_chip.x, eta_chip.y, eta_chip.width, eta_chip.height, 10, p.card);
        canvas.stroke_round_rect(eta_chip.x, eta_chip.y, eta_chip.width, eta_chip.height, 10, p.card_border);
        canvas.draw_text(font_small, eta_chip.x + 10, eta_chip.y + 14, eta_text, p.subtext);
    }
}

}  // namespace bikeos
