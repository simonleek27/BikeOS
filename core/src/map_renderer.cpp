#include "bikeos/map_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "bikeos/geo.hpp"

namespace bikeos {
namespace {

constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

int region_code(const MapRenderer::ScreenPoint point)
{
    int code = 0;
    if (point.x < 0) code |= 1;
    else if (point.x >= Framebuffer::width) code |= 2;
    if (point.y < 0) code |= 4;
    else if (point.y >= MapRenderer::map_height) code |= 8;
    return code;
}

char maneuver_symbol(const std::optional<Maneuver>& maneuver)
{
    if (!maneuver) return '-';
    switch (maneuver->type) {
        case ManeuverType::left:
        case ManeuverType::slight_left:
        case ManeuverType::sharp_left: return '<';
        case ManeuverType::right:
        case ManeuverType::slight_right:
        case ManeuverType::sharp_right: return '>';
        case ManeuverType::destination: return 'A';
        case ManeuverType::u_turn: return 'U';
        case ManeuverType::roundabout: return 'O';
        case ManeuverType::straight: return '^';
    }
    return '?';
}

}  // namespace

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
        .y = static_cast<int>(std::lround(map_height / 2.0 - north / scale)),
    };
}

bool MapRenderer::clip_line(ScreenPoint& a, ScreenPoint& b)
{
    int code_a = region_code(a);
    int code_b = region_code(b);
    while (true) {
        if ((code_a | code_b) == 0) return true;
        if ((code_a & code_b) != 0) return false;
        const int outside = code_a != 0 ? code_a : code_b;
        double x = 0.0;
        double y = 0.0;
        if (outside & 8) {
            y = map_height - 1;
            x = a.x + static_cast<double>(b.x - a.x) * (y - a.y) / (b.y - a.y);
        } else if (outside & 4) {
            y = 0;
            x = a.x + static_cast<double>(b.x - a.x) * (y - a.y) / (b.y - a.y);
        } else if (outside & 2) {
            x = Framebuffer::width - 1;
            y = a.y + static_cast<double>(b.y - a.y) * (x - a.x) / (b.x - a.x);
        } else {
            x = 0;
            y = a.y + static_cast<double>(b.y - a.y) * (x - a.x) / (b.x - a.x);
        }
        ScreenPoint& point = outside == code_a ? a : b;
        point = {.x = static_cast<int>(std::lround(x)), .y = static_cast<int>(std::lround(y))};
        if (outside == code_a) code_a = region_code(a);
        else code_b = region_code(b);
    }
}

void MapRenderer::draw_position(Framebuffer& framebuffer, const ScreenPoint point, const float heading_deg)
{
    // Clear a halo so the direction marker remains visible over a thick route.
    framebuffer.fill_rect(point.x - 7, point.y - 7, 15, 15, false);
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

void MapRenderer::draw_status(Framebuffer& framebuffer, const NavigationSnapshot& navigation)
{
    framebuffer.fill_rect(0, map_height, Framebuffer::width, Framebuffer::height - map_height, true);
    char left[16]{};
    char right[16]{};
    if (navigation.state == NavigationState::destination_reached) {
        std::snprintf(left, sizeof(left), "ARRIVED");
    } else if (navigation.state == NavigationState::off_route ||
               navigation.state == NavigationState::rerouting ||
               navigation.state == NavigationState::phone_disconnected) {
        std::snprintf(left, sizeof(left), "%s", navigation_state_name(navigation.state));
    } else {
        std::snprintf(left, sizeof(left), "%c %uM", maneuver_symbol(navigation.next_maneuver),
                      static_cast<unsigned>(std::lround(navigation.distance_to_maneuver_m)));
    }
    std::snprintf(right, sizeof(right), "%s %s", navigation.gps_valid ? "G" : "X",
                  navigation.phone_connected ? "B" : "-");
    framebuffer.text_3x5(2, 56, left, false);
    const int right_width = static_cast<int>(std::char_traits<char>::length(right)) * 4 - 1;
    framebuffer.text_3x5(Framebuffer::width - right_width - 2, 56, right, false);
}

void MapRenderer::render(Framebuffer& framebuffer,
                         const MapDataset& map,
                         const std::span<const GeoPointE7> route,
                         const NavigationSnapshot& navigation,
                         const Viewport& viewport,
                         const RenderOptions& options) const
{
    framebuffer.clear(false);
    for (const auto& road : map.roads) {
        for (std::size_t i = 1; i < road.points.size(); ++i) {
            ScreenPoint a = to_screen(road.points[i - 1], viewport);
            ScreenPoint b = to_screen(road.points[i], viewport);
            if (!clip_line(a, b)) continue;
            if (road.road_class == RoadClass::cycleway || road.road_class == RoadClass::footway) {
                const int length = std::max(std::abs(b.x - a.x), std::abs(b.y - a.y));
                if (length < 3 || ((a.x + a.y) & 1) == 0) framebuffer.line(a.x, a.y, b.x, b.y, true);
            } else {
                framebuffer.line(a.x, a.y, b.x, b.y, true);
            }
        }
    }

    if (options.show_route) {
        for (std::size_t i = 1; i < route.size(); ++i) {
            ScreenPoint a = to_screen(route[i - 1], viewport);
            ScreenPoint b = to_screen(route[i], viewport);
            if (clip_line(a, b)) framebuffer.thick_line(a.x, a.y, b.x, b.y, 3, true);
        }
    }
    if (navigation.gps_valid) {
        const float marker_heading = viewport.heading_up ? 0.0F
                                                         : (navigation.heading_valid ? navigation.heading_deg : 0.0F);
        draw_position(framebuffer, to_screen(navigation.position, viewport), marker_heading);
    }
    if (options.show_status) draw_status(framebuffer, navigation);
}

}  // namespace bikeos
