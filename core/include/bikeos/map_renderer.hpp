#pragma once

#include <span>
#include <cstdint>

#include "bikeos/framebuffer.hpp"
#include "bikeos/map_data.hpp"
#include "bikeos/navigation.hpp"

namespace bikeos {

struct Viewport {
    GeoPointE7 center{};
    float meters_per_pixel{4.0F};
    bool heading_up{};
    float heading_deg{};
    bool automatic_zoom{true};
    int map_top{11};
    int map_height{41};
};

enum class DisplayMode : std::uint8_t {
    automatic,
    normal,
    approaching,
    turn_now,
};

struct RenderOptions {
    bool show_route{true};
    bool show_status{true};
    DisplayMode mode{DisplayMode::automatic};
};

class MapRenderer {
public:
    static constexpr int normal_map_top = 11;
    static constexpr int normal_map_height = 41;

    struct ScreenPoint {
        int x{};
        int y{};
    };

    void render(Framebuffer& framebuffer,
                const MapDataset& map,
                std::span<const GeoPointE7> route,
                const NavigationSnapshot& navigation,
                const Viewport& viewport,
                const RenderOptions& options = {}) const;

    static float zoom_for_distance(float distance_to_maneuver_m);

private:
    static ScreenPoint to_screen(GeoPointE7 point, const Viewport& viewport);
    static bool clip_line(ScreenPoint& a, ScreenPoint& b, int top, int bottom);
    static void draw_position(Framebuffer& framebuffer, ScreenPoint point, float heading_deg,
                              int top, int bottom);
    static void draw_instruction(Framebuffer& framebuffer, const NavigationSnapshot& navigation,
                                 const std::span<const GeoPointE7> route, int top, int bottom,
                                 DisplayMode mode);
    static void draw_status_banner(Framebuffer& framebuffer, const NavigationSnapshot& navigation,
                                   int top, int bottom);

    mutable float smoothed_zoom_mpp_{4.0F};
    mutable bool zoom_initialized_{};
};

}  // namespace bikeos
