#include "bikeos/map_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>

#include "bikeos/geo.hpp"

namespace bikeos {
namespace {

constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

struct Layout {
    int map_left{};
    int map_right{Framebuffer::width};
    int map_top{};
    int map_bottom{};
    bool turn_panel{};
};

struct Label {
    std::array<char, 32> bytes{};
    std::size_t length{};
};

Label fit_label(const std::string_view text, const int maximum_pixels)
{
    Label result;
    const std::size_t maximum = std::min<std::size_t>(result.bytes.size(),
                                                       maximum_pixels > 0
                                                           ? static_cast<std::size_t>((maximum_pixels + 5) / 6)
                                                           : 0);
    if (maximum == 0) return result;
    if (text.size() <= maximum) {
        result.length = text.size();
        std::copy_n(text.data(), result.length, result.bytes.data());
        return result;
    }
    if (maximum < 4) {
        result.length = maximum;
        std::copy_n(text.data(), result.length, result.bytes.data());
        return result;
    }
    result.length = maximum;
    const std::size_t prefix = maximum - 3;
    std::copy_n(text.data(), prefix, result.bytes.data());
    result.bytes[prefix] = '.';
    result.bytes[prefix + 1] = '.';
    result.bytes[prefix + 2] = '.';
    return result;
}

void draw_label(Framebuffer& framebuffer, const int x, const int y, const Label& label)
{
    framebuffer.text_5x7(x, y, std::string_view(label.bytes.data(), label.length), false, 1);
}

GeoPointE7 interpolate_geo(const GeoPointE7 a, const GeoPointE7 b, const double t)
{
    return {
        .latitude_e7 = static_cast<std::int32_t>(std::llround(a.latitude_e7 + (b.latitude_e7 - a.latitude_e7) * t)),
        .longitude_e7 = static_cast<std::int32_t>(std::llround(a.longitude_e7 + (b.longitude_e7 - a.longitude_e7) * t)),
    };
}

DisplayMode selected_mode(const NavigationSnapshot& navigation, const RenderOptions& options)
{
    if (options.mode != DisplayMode::automatic) return options.mode;
    if (navigation.state != NavigationState::navigating &&
        navigation.state != NavigationState::phone_disconnected) return DisplayMode::normal;
    if (navigation.distance_to_maneuver_m <= 20.0F) return DisplayMode::turn_now;
    if (navigation.distance_to_maneuver_m <= 70.0F) return DisplayMode::approaching;
    return DisplayMode::normal;
}

bool shows_turn_panel(const NavigationSnapshot& navigation, const RenderOptions& options)
{
    return options.show_status && navigation.next_maneuver &&
           (navigation.state == NavigationState::navigating ||
            navigation.state == NavigationState::phone_disconnected);
}

Layout layout_for(const bool turn_panel)
{
    if (turn_panel) {
        return {.map_left = MapRenderer::panel_width + 1,
                .map_right = Framebuffer::width,
                .map_top = 0,
                .map_bottom = Framebuffer::height - MapRenderer::street_strip_height,
                .turn_panel = true};
    }
    return {.map_top = MapRenderer::normal_map_top,
            .map_bottom = MapRenderer::normal_map_top + MapRenderer::normal_map_height};
}

int region_code(const MapRenderer::ScreenPoint point, const int left, const int right,
                const int top, const int bottom)
{
    int code = 0;
    if (point.x < left) code |= 1;
    else if (point.x >= right) code |= 2;
    if (point.y < top) code |= 4;
    else if (point.y >= bottom) code |= 8;
    return code;
}

void fill_triangle(Framebuffer& framebuffer, const MapRenderer::ScreenPoint a,
                   const MapRenderer::ScreenPoint b, const MapRenderer::ScreenPoint c, const bool on)
{
    const auto edge = [](const MapRenderer::ScreenPoint p, const MapRenderer::ScreenPoint q, const int x, const int y) {
        return (q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x);
    };
    const int min_x = std::min({a.x, b.x, c.x});
    const int max_x = std::max({a.x, b.x, c.x});
    const int min_y = std::min({a.y, b.y, c.y});
    const int max_y = std::max({a.y, b.y, c.y});
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const int e0 = edge(a, b, x, y);
            const int e1 = edge(b, c, x, y);
            const int e2 = edge(c, a, x, y);
            if ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0)) {
                framebuffer.set_pixel(x, y, on);
            }
        }
    }
}

// Arrowhead whose tip sits 6 px past `from` along the unit direction (dx, dy).
void draw_arrowhead(Framebuffer& framebuffer, const MapRenderer::ScreenPoint from,
                    const double dx, const double dy, const bool on)
{
    const auto at = [&](const double along, const double across) {
        return MapRenderer::ScreenPoint{
            .x = from.x + static_cast<int>(std::lround(dx * along - dy * across)),
            .y = from.y + static_cast<int>(std::lround(dy * along + dx * across)),
        };
    };
    fill_triangle(framebuffer, at(7.0, 0.0), at(-1.0, 7.0), at(-1.0, -7.0), on);
}

// Turn arrow filling roughly a 36 x 34 px box whose top-left corner is (x, y).
void draw_large_arrow(Framebuffer& framebuffer, const int x, const int y, const ManeuverType type, const bool on)
{
    using Point = MapRenderer::ScreenPoint;
    constexpr int shaft = 5;
    if (type == ManeuverType::destination) {
        framebuffer.thick_line(x + 12, y + 33, x + 12, y + 2, 3, on);
        fill_triangle(framebuffer, {x + 13, y + 2}, {x + 31, y + 9}, {x + 13, y + 16}, on);
        return;
    }
    if (type == ManeuverType::u_turn) {
        framebuffer.thick_line(x + 26, y + 33, x + 26, y + 10, shaft, on);
        framebuffer.thick_line(x + 26, y + 10, x + 12, y + 10, shaft, on);
        framebuffer.thick_line(x + 12, y + 10, x + 12, y + 22, shaft, on);
        draw_arrowhead(framebuffer, {x + 12, y + 23}, 0.0, 1.0, on);
        return;
    }
    if (type == ManeuverType::roundabout) {
        const Point centre{x + 18, y + 20};
        for (int dy = -9; dy <= 9; ++dy) {
            for (int dx = -9; dx <= 9; ++dx) {
                const int r2 = dx * dx + dy * dy;
                if (r2 >= 36 && r2 <= 81) framebuffer.set_pixel(centre.x + dx, centre.y + dy, on);
            }
        }
        framebuffer.thick_line(centre.x, y + 33, centre.x, centre.y + 9, shaft, on);
        framebuffer.thick_line(centre.x, centre.y - 9, centre.x, y + 8, shaft, on);
        draw_arrowhead(framebuffer, {centre.x, y + 8}, 0.0, -1.0, on);
        return;
    }

    double degrees = 0.0;  // 0 is straight ahead, positive turns right.
    switch (type) {
    case ManeuverType::left: degrees = -90.0; break;
    case ManeuverType::right: degrees = 90.0; break;
    case ManeuverType::slight_left: degrees = -45.0; break;
    case ManeuverType::slight_right: degrees = 45.0; break;
    case ManeuverType::sharp_left: degrees = -135.0; break;
    case ManeuverType::sharp_right: degrees = 135.0; break;
    default: break;
    }
    const double radians = degrees * kDegreesToRadians;
    const double dx = std::sin(radians);
    const double dy = -std::cos(radians);
    // Shift the shaft away from the turn so the arm and head stay inside the box.
    const int shaft_x = x + 18 - static_cast<int>(std::lround(dx * 6.0));
    const Point elbow{shaft_x, y + 20};
    const double arm = degrees == 0.0 ? 7.0 : 11.0;
    const Point end{elbow.x + static_cast<int>(std::lround(dx * arm)),
                    elbow.y + static_cast<int>(std::lround(dy * arm))};
    framebuffer.thick_line(shaft_x, y + 33, elbow.x, elbow.y, shaft, on);
    framebuffer.thick_line(elbow.x, elbow.y, end.x, end.y, shaft, on);
    draw_arrowhead(framebuffer, end, dx, dy, on);
}

}  // namespace

float MapRenderer::zoom_for_distance(const float distance_to_maneuver_m)
{
    if (distance_to_maneuver_m > 200.0F) return 6.0F;
    if (distance_to_maneuver_m > 80.0F) return 4.5F;
    if (distance_to_maneuver_m > 30.0F) return 3.2F;
    return 2.2F;
}

static float hysteresis_zoom_target(const float distance_to_maneuver_m, const float current_zoom)
{
    // Keep each zoom band alive across a small distance change so the map does
    // not snap back and forth while a GPS fix jitters around a threshold.
    if (current_zoom > 5.25F) return distance_to_maneuver_m > 160.0F ? 6.0F : 4.5F;
    if (current_zoom > 3.85F) {
        if (distance_to_maneuver_m > 220.0F) return 6.0F;
        if (distance_to_maneuver_m > 60.0F) return 4.5F;
        return 3.2F;
    }
    if (current_zoom > 2.7F) {
        if (distance_to_maneuver_m > 95.0F) return 4.5F;
        if (distance_to_maneuver_m > 24.0F) return 3.2F;
        return 2.2F;
    }
    return distance_to_maneuver_m > 38.0F ? 3.2F : 2.2F;
}

MapRenderer::ScreenPoint MapRenderer::to_screen(const GeoPointE7 point, const Viewport& viewport)
{
    const LocalPointM local = project_local(point, viewport.center);
    double east = local.east_m;
    double north = local.north_m;
    if (viewport.heading_up) {
        // Rotate by +heading so the direction of travel points up the screen.
        const double angle = viewport.heading_deg * kDegreesToRadians;
        const double rotated_east = east * std::cos(angle) - north * std::sin(angle);
        const double rotated_north = east * std::sin(angle) + north * std::cos(angle);
        east = rotated_east;
        north = rotated_north;
    }
    const double scale = std::max(0.25F, viewport.meters_per_pixel);
    return {
        .x = static_cast<int>(std::lround(viewport.map_left + viewport.map_width / 2.0 + east / scale)),
        .y = static_cast<int>(std::lround(viewport.map_top + viewport.map_height / 2.0 - north / scale)),
    };
}

bool MapRenderer::clip_line(ScreenPoint& a, ScreenPoint& b, const int left, const int right,
                            const int top, const int bottom)
{
    int code_a = region_code(a, left, right, top, bottom);
    int code_b = region_code(b, left, right, top, bottom);
    while (true) {
        if ((code_a | code_b) == 0) return true;
        if ((code_a & code_b) != 0) return false;
        const int outside = code_a != 0 ? code_a : code_b;
        double x = 0.0;
        double y = 0.0;
        if ((outside & 8) != 0 && b.y != a.y) {
            y = bottom - 1;
            x = a.x + static_cast<double>(b.x - a.x) * (y - a.y) / (b.y - a.y);
        } else if ((outside & 4) != 0 && b.y != a.y) {
            y = top;
            x = a.x + static_cast<double>(b.x - a.x) * (y - a.y) / (b.y - a.y);
        } else if ((outside & 2) != 0 && b.x != a.x) {
            x = right - 1;
            y = a.y + static_cast<double>(b.y - a.y) * (x - a.x) / (b.x - a.x);
        } else if (b.x != a.x) {
            x = left;
            y = a.y + static_cast<double>(b.y - a.y) * (x - a.x) / (b.x - a.x);
        } else {
            return false;
        }
        ScreenPoint& point = outside == code_a ? a : b;
        point = {.x = static_cast<int>(std::lround(x)), .y = static_cast<int>(std::lround(y))};
        if (outside == code_a) code_a = region_code(a, left, right, top, bottom);
        else code_b = region_code(b, left, right, top, bottom);
    }
}

void MapRenderer::draw_position(Framebuffer& framebuffer, const ScreenPoint point, const float heading_deg,
                                const int left, const int right, const int top, const int bottom)
{
    if (point.x < left || point.x >= right || point.y < top || point.y >= bottom) return;
    framebuffer.fill_rect(point.x - 6, point.y - 6, 13, 13, false);
    const double radians = (heading_deg - 90.0) * kDegreesToRadians;
    const auto vertex = [&](const double angle, const double distance) {
        return ScreenPoint{
            .x = point.x + static_cast<int>(std::lround(std::cos(radians + angle) * distance)),
            .y = point.y + static_cast<int>(std::lround(std::sin(radians + angle) * distance)),
        };
    };
    const ScreenPoint tip = vertex(0.0, 6.0);
    const ScreenPoint left_wing = vertex(2.45, 4.5);
    const ScreenPoint right_wing = vertex(-2.45, 4.5);
    framebuffer.thick_line(tip.x, tip.y, left_wing.x, left_wing.y, 2, true);
    framebuffer.thick_line(left_wing.x, left_wing.y, right_wing.x, right_wing.y, 2, true);
    framebuffer.thick_line(right_wing.x, right_wing.y, tip.x, tip.y, 2, true);
    framebuffer.line(tip.x, tip.y, point.x, point.y, true);
}

void MapRenderer::draw_status_banner(Framebuffer& framebuffer, const NavigationSnapshot& navigation,
                                     const int top, const int bottom)
{
    framebuffer.fill_rect(0, top, Framebuffer::width, bottom - top, true);
    const char* title = navigation_state_name(navigation.state);
    const Label label = fit_label(title, Framebuffer::width - 4);
    draw_label(framebuffer, 2, top + 2, label);
}

void MapRenderer::draw_instruction(Framebuffer& framebuffer, const NavigationSnapshot& navigation,
                                   const int top, const int bottom)
{
    framebuffer.fill_rect(0, 0, Framebuffer::width, top, true);
    framebuffer.fill_rect(0, bottom, Framebuffer::width, Framebuffer::height - bottom, true);
    draw_status_banner(framebuffer, navigation, 0, top);
    const Label footer = fit_label(navigation.state == NavigationState::gps_lost ? "WAITING FOR GPS" :
                                       navigation.state == NavigationState::destination_reached ? "RIDE COMPLETE" :
                                       navigation.state == NavigationState::phone_disconnected ? "PHONE DISCONNECTED" :
                                       navigation.state == NavigationState::rerouting ? "REQUESTING NEW ROUTE" :
                                       navigation.state == NavigationState::off_route ? "RETURN TO ROUTE" : "CHECK ROUTE",
                                   Framebuffer::width - 4);
    draw_label(framebuffer, 2, bottom + 2, footer);
}

void MapRenderer::draw_turn_panel(Framebuffer& framebuffer, const NavigationSnapshot& navigation,
                                  const DisplayMode mode, const int map_bottom)
{
    // The panel inverts in the last metres so "turn now" reads without looking at the digits.
    const bool turn_now = mode == DisplayMode::turn_now;
    const bool background = !turn_now;
    framebuffer.fill_rect(0, 0, panel_width, Framebuffer::height, background);
    framebuffer.fill_rect(panel_width, 0, 1, Framebuffer::height, false);

    const Maneuver& maneuver = *navigation.next_maneuver;
    draw_large_arrow(framebuffer, 2, 2, maneuver.type, !background);

    const float metres = std::max(0.0F, navigation.distance_to_maneuver_m);
    char number[8]{};
    const char* unit = "M";
    if (metres < 1000.0F) {
        // Round to 10 m above 100 m; GPS is not better than that and the digits jump less.
        const unsigned value = metres < 100.0F ? static_cast<unsigned>(std::lround(metres))
                                               : static_cast<unsigned>(std::lround(metres / 10.0F)) * 10U;
        if (value >= 1000U) {
            std::snprintf(number, sizeof(number), "1.0");
            unit = "KM";
        } else {
            std::snprintf(number, sizeof(number), "%u", value);
        }
    } else if (metres < 10000.0F) {
        std::snprintf(number, sizeof(number), "%.1f", static_cast<double>(metres / 1000.0F));
        unit = "KM";
    } else {
        std::snprintf(number, sizeof(number), "%u", static_cast<unsigned>(std::lround(metres / 1000.0F)));
        unit = "KM";
    }
    const std::string_view digits(number);
    const int digits_width = Framebuffer::text_5x7_scaled_width(digits, 2);
    framebuffer.text_5x7_scaled((panel_width - digits_width) / 2, 40, digits, 2, !background);
    const int unit_width = Framebuffer::text_5x7_width(unit);
    framebuffer.text_5x7((panel_width - unit_width) / 2, 56, unit, !background);

    const int strip_left = panel_width + 1;
    framebuffer.fill_rect(strip_left, map_bottom, Framebuffer::width - strip_left,
                          Framebuffer::height - map_bottom, true);
    const std::string_view street = maneuver.street_name.empty() ? std::string_view(maneuver_name(maneuver.type))
                                                                 : std::string_view(maneuver.street_name);
    draw_label(framebuffer, strip_left + 2, map_bottom + 1, fit_label(street, Framebuffer::width - strip_left - 3));
}

void MapRenderer::render(Framebuffer& framebuffer,
                         const MapDataset& map,
                         const std::span<const GeoPointE7> route,
                         const NavigationSnapshot& navigation,
                         const Viewport& viewport,
                         const RenderOptions& options) const
{
    const DisplayMode mode = selected_mode(navigation, options);
    const Layout layout = layout_for(shows_turn_panel(navigation, options));
    Viewport effective = viewport;
    effective.map_left = layout.map_left;
    effective.map_width = layout.map_right - layout.map_left;
    effective.map_top = layout.map_top;
    effective.map_height = layout.map_bottom - layout.map_top;
    if (effective.automatic_zoom && navigation.next_maneuver) {
        if (!zoom_initialized_) {
            smoothed_zoom_mpp_ = zoom_for_distance(navigation.distance_to_maneuver_m);
            zoom_initialized_ = true;
        }
        const float target = hysteresis_zoom_target(navigation.distance_to_maneuver_m, smoothed_zoom_mpp_);
        smoothed_zoom_mpp_ += (target - smoothed_zoom_mpp_) * 0.25F;
        effective.meters_per_pixel = smoothed_zoom_mpp_;
    }
    if (effective.heading_up) {
        if (navigation.heading_valid) {
            last_heading_deg_ = navigation.heading_deg;
            have_heading_ = true;
        }
        // Until the first good heading arrives, stay north-up rather than guess.
        effective.heading_up = have_heading_;
        effective.heading_deg = last_heading_deg_;
    }
    if (navigation.gps_valid) {
        effective.center = navigation.position;
        if (navigation.next_maneuver && navigation.distance_to_maneuver_m < 220.0F &&
            navigation.next_maneuver->point_index < route.size()) {
            effective.center = interpolate_geo(navigation.position,
                                               route[navigation.next_maneuver->point_index], 0.35);
        }
    } else if (valid_coordinate(navigation.matched_position)) {
        effective.center = navigation.matched_position;
    }

    framebuffer.clear(false);
    for (const auto& road : map.roads) {
        if (road.road_class == RoadClass::service || road.road_class == RoadClass::footway ||
            road.road_class == RoadClass::other || (road.road_class == RoadClass::cycleway && road.name.empty()) ||
            (effective.meters_per_pixel > 5.5F && road.road_class == RoadClass::residential)) {
            continue;
        }
        for (std::size_t i = 1; i < road.points.size(); ++i) {
            ScreenPoint a = to_screen(road.points[i - 1], effective);
            ScreenPoint b = to_screen(road.points[i], effective);
            if (!clip_line(a, b, layout.map_left, layout.map_right, layout.map_top, layout.map_bottom)) continue;
            framebuffer.line(a.x, a.y, b.x, b.y, true);
        }
    }

    if (options.show_route) {
        for (std::size_t i = 1; i < route.size(); ++i) {
            ScreenPoint a = to_screen(route[i - 1], effective);
            ScreenPoint b = to_screen(route[i], effective);
            if (!clip_line(a, b, layout.map_left, layout.map_right, layout.map_top, layout.map_bottom)) continue;
            // Clear a halo first so streets crossing the route never touch it.
            framebuffer.thick_line(a.x, a.y, b.x, b.y, 9, false);
        }
        for (std::size_t i = 1; i < route.size(); ++i) {
            ScreenPoint a = to_screen(route[i - 1], effective);
            ScreenPoint b = to_screen(route[i], effective);
            if (!clip_line(a, b, layout.map_left, layout.map_right, layout.map_top, layout.map_bottom)) continue;
            const bool passed = navigation.gps_valid && i <= navigation.matched_segment;
            const bool immediate = navigation.next_maneuver &&
                                   i >= navigation.matched_segment &&
                                   i <= navigation.next_maneuver->point_index;
            // thick_line widths are odd: 5 px up to the next turn, 3 px beyond it, 1 px behind the rider.
            framebuffer.thick_line(a.x, a.y, b.x, b.y, immediate ? 5 : (passed ? 1 : 3), true);
        }
        if (navigation.next_maneuver && navigation.next_maneuver->point_index < route.size()) {
            const ScreenPoint turn = to_screen(route[navigation.next_maneuver->point_index], effective);
            if (turn.x >= layout.map_left + 4 && turn.x < layout.map_right - 4 &&
                turn.y >= layout.map_top + 4 && turn.y < layout.map_bottom - 4) {
                framebuffer.fill_rect(turn.x - 4, turn.y - 4, 9, 9, false);
                framebuffer.fill_rect(turn.x - 2, turn.y - 2, 5, 5, true);
            }
        }
    }
    if (navigation.gps_valid) {
        const float marker_heading = effective.heading_up ? 0.0F
                                                          : (navigation.heading_valid ? navigation.heading_deg : 0.0F);
        draw_position(framebuffer, to_screen(navigation.position, effective), marker_heading,
                      layout.map_left, layout.map_right, layout.map_top, layout.map_bottom);
    }
    if (layout.turn_panel) {
        draw_turn_panel(framebuffer, navigation, mode, layout.map_bottom);
    } else if (options.show_status) {
        draw_instruction(framebuffer, navigation, layout.map_top, layout.map_bottom);
    }
}

}  // namespace bikeos
