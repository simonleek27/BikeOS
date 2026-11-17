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
    int map_top{};
    int map_bottom{};
    int footer_top{};
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

Layout layout_for(const DisplayMode mode)
{
    if (mode == DisplayMode::turn_now) return {.map_top = 18, .map_bottom = 50, .footer_top = 50};
    return {.map_top = MapRenderer::normal_map_top,
            .map_bottom = MapRenderer::normal_map_top + MapRenderer::normal_map_height,
            .footer_top = MapRenderer::normal_map_top + MapRenderer::normal_map_height};
}

int region_code(const MapRenderer::ScreenPoint point, const int top, const int bottom)
{
    int code = 0;
    if (point.x < 0) code |= 1;
    else if (point.x >= Framebuffer::width) code |= 2;
    if (point.y < top) code |= 4;
    else if (point.y >= bottom) code |= 8;
    return code;
}

void draw_large_arrow(Framebuffer& framebuffer, const int x, const int y, const ManeuverType type)
{
    if (type == ManeuverType::left || type == ManeuverType::slight_left || type == ManeuverType::sharp_left) {
        framebuffer.thick_line(x + 14, y + 3, x + 4, y + 3, 2, true);
        framebuffer.thick_line(x + 4, y + 3, x + 10, y - 3, 2, true);
        framebuffer.thick_line(x + 4, y + 3, x + 10, y + 9, 2, true);
    } else if (type == ManeuverType::right || type == ManeuverType::slight_right || type == ManeuverType::sharp_right) {
        framebuffer.thick_line(x + 2, y + 3, x + 12, y + 3, 2, true);
        framebuffer.thick_line(x + 12, y + 3, x + 6, y - 3, 2, true);
        framebuffer.thick_line(x + 12, y + 3, x + 6, y + 9, 2, true);
    } else {
        framebuffer.thick_line(x + 7, y + 10, x + 7, y - 2, 2, true);
        framebuffer.thick_line(x + 7, y - 2, x + 1, y + 4, 2, true);
        framebuffer.thick_line(x + 7, y - 2, x + 13, y + 4, 2, true);
    }
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
        const double angle = -viewport.heading_deg * kDegreesToRadians;
        const double rotated_east = east * std::cos(angle) - north * std::sin(angle);
        const double rotated_north = east * std::sin(angle) + north * std::cos(angle);
        east = rotated_east;
        north = rotated_north;
    }
    const double scale = std::max(0.25F, viewport.meters_per_pixel);
    return {
        .x = static_cast<int>(std::lround(Framebuffer::width / 2.0 + east / scale)),
        .y = static_cast<int>(std::lround(viewport.map_top + viewport.map_height / 2.0 - north / scale)),
    };
}

bool MapRenderer::clip_line(ScreenPoint& a, ScreenPoint& b, const int top, const int bottom)
{
    int code_a = region_code(a, top, bottom);
    int code_b = region_code(b, top, bottom);
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
            x = Framebuffer::width - 1;
            y = a.y + static_cast<double>(b.y - a.y) * (x - a.x) / (b.x - a.x);
        } else if (b.x != a.x) {
            x = 0;
            y = a.y + static_cast<double>(b.y - a.y) * (x - a.x) / (b.x - a.x);
        } else {
            return false;
        }
        ScreenPoint& point = outside == code_a ? a : b;
        point = {.x = static_cast<int>(std::lround(x)), .y = static_cast<int>(std::lround(y))};
        if (outside == code_a) code_a = region_code(a, top, bottom);
        else code_b = region_code(b, top, bottom);
    }
}

void MapRenderer::draw_position(Framebuffer& framebuffer, const ScreenPoint point, const float heading_deg,
                                const int top, const int bottom)
{
    if (point.y < top || point.y >= bottom) return;
    framebuffer.fill_rect(point.x - 6, point.y - 6, 13, 13, false);
    const double radians = (heading_deg - 90.0) * kDegreesToRadians;
    const auto vertex = [&](const double angle, const double distance) {
        return ScreenPoint{
            .x = point.x + static_cast<int>(std::lround(std::cos(radians + angle) * distance)),
            .y = point.y + static_cast<int>(std::lround(std::sin(radians + angle) * distance)),
        };
    };
    const ScreenPoint tip = vertex(0.0, 6.0);
    const ScreenPoint left = vertex(2.45, 4.5);
    const ScreenPoint right = vertex(-2.45, 4.5);
    framebuffer.thick_line(tip.x, tip.y, left.x, left.y, 2, true);
    framebuffer.thick_line(left.x, left.y, right.x, right.y, 2, true);
    framebuffer.thick_line(right.x, right.y, tip.x, tip.y, 2, true);
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
                                   const std::span<const GeoPointE7> route, const int top, const int bottom,
                                   const DisplayMode mode)
{
    const int footer_top = mode == DisplayMode::turn_now ? 50 : bottom;
    framebuffer.fill_rect(0, 0, Framebuffer::width, top, true);
    framebuffer.fill_rect(0, footer_top, Framebuffer::width, Framebuffer::height - footer_top, true);

    if (navigation.state == NavigationState::gps_lost || navigation.state == NavigationState::waiting_for_gps ||
        navigation.state == NavigationState::rerouting || navigation.state == NavigationState::off_route ||
        navigation.state == NavigationState::phone_disconnected || navigation.state == NavigationState::destination_reached ||
        navigation.state == NavigationState::error || navigation.state == NavigationState::no_route) {
        draw_status_banner(framebuffer, navigation, 0, top);
        const Label footer = fit_label(navigation.state == NavigationState::gps_lost ? "WAITING FOR GPS" :
                                           navigation.state == NavigationState::destination_reached ? "RIDE COMPLETE" :
                                           navigation.state == NavigationState::phone_disconnected ? "PHONE DISCONNECTED" :
                                           navigation.state == NavigationState::rerouting ? "REQUESTING NEW ROUTE" :
                                           navigation.state == NavigationState::off_route ? "RETURN TO ROUTE" : "CHECK ROUTE",
                                       Framebuffer::width - 4);
        draw_label(framebuffer, 2, footer_top + 2, footer);
        return;
    }

    if (!navigation.next_maneuver) {
        draw_status_banner(framebuffer, navigation, 0, top);
        return;
    }
    const Maneuver& maneuver = *navigation.next_maneuver;
    const Label direction = fit_label(maneuver_name(maneuver.type), Framebuffer::width - 4);
    if (mode == DisplayMode::turn_now) {
        draw_label(framebuffer, 2, 1, direction);
        const Label name = fit_label(maneuver.street_name, Framebuffer::width - 4);
        draw_label(framebuffer, 2, 9, name);
    } else {
        std::array<char, 32> text{};
        const std::string_view prefix = maneuver.street_name.empty() ? std::string_view("NEXT ") : std::string_view("TO ");
        const std::string_view value = maneuver.street_name.empty() ? std::string_view(maneuver_name(maneuver.type)) : maneuver.street_name;
        const std::size_t max_chars = std::min<std::size_t>(text.size() - 1, 20);
        std::size_t length = 0;
        for (const char c : prefix) if (length < max_chars) text[length++] = c;
        for (const char c : value) if (length < max_chars) text[length++] = c;
        draw_label(framebuffer, 2, 2, fit_label(std::string_view(text.data(), length), Framebuffer::width - 4));
    }

    draw_large_arrow(framebuffer, 3, footer_top + 2, maneuver.type);
    char distance[16]{};
    std::snprintf(distance, sizeof(distance), "%um", static_cast<unsigned>(std::lround(navigation.distance_to_maneuver_m)));
    draw_label(framebuffer, 22, footer_top + 2, fit_label(distance, 50));
    const Label gps = fit_label(navigation.gps_valid ? "GPS" : "NO GPS", 34);
    draw_label(framebuffer, 92, footer_top + 2, gps);
    (void)route;
}

void MapRenderer::render(Framebuffer& framebuffer,
                         const MapDataset& map,
                         const std::span<const GeoPointE7> route,
                         const NavigationSnapshot& navigation,
                         const Viewport& viewport,
                         const RenderOptions& options) const
{
    const DisplayMode mode = selected_mode(navigation, options);
    const Layout layout = layout_for(mode);
    Viewport effective = viewport;
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
            if (!clip_line(a, b, layout.map_top, layout.map_bottom)) continue;
            framebuffer.line(a.x, a.y, b.x, b.y, true);
        }
    }

    if (options.show_route) {
        for (std::size_t i = 1; i < route.size(); ++i) {
            ScreenPoint a = to_screen(route[i - 1], effective);
            ScreenPoint b = to_screen(route[i], effective);
            if (!clip_line(a, b, layout.map_top, layout.map_bottom)) continue;
            framebuffer.thick_line(a.x, a.y, b.x, b.y, 5, false);
            const bool passed = navigation.gps_valid && i <= navigation.matched_segment;
            const bool immediate = navigation.next_maneuver &&
                                   i >= navigation.matched_segment &&
                                   i <= navigation.next_maneuver->point_index;
            framebuffer.thick_line(a.x, a.y, b.x, b.y, immediate ? 3 : (passed ? 1 : 2), true);
        }
        if (navigation.next_maneuver && navigation.next_maneuver->point_index < route.size()) {
            const ScreenPoint turn = to_screen(route[navigation.next_maneuver->point_index], effective);
            framebuffer.fill_rect(turn.x - 3, turn.y - 3, 7, 7, false);
            framebuffer.fill_rect(turn.x - 1, turn.y - 1, 3, 3, true);
        }
    }
    if (navigation.gps_valid) {
        const float marker_heading = effective.heading_up ? 0.0F
                                                          : (navigation.heading_valid ? navigation.heading_deg : 0.0F);
        draw_position(framebuffer, to_screen(navigation.position, effective), marker_heading,
                      layout.map_top, layout.map_bottom);
    }
    if (options.show_status) draw_instruction(framebuffer, navigation, route, layout.map_top, layout.map_bottom, mode);
}

}  // namespace bikeos
