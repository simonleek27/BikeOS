#pragma once

#include <span>

#include "bikeos/framebuffer.hpp"
#include "bikeos/map_data.hpp"
#include "bikeos/navigation.hpp"

namespace bikeos {

struct Viewport {
    GeoPointE7 center{};
    float meters_per_pixel{4.0F};
    bool heading_up{};
    float heading_deg{};
};

struct RenderOptions {
    bool show_route{true};
    bool show_status{true};
};

class MapRenderer {
public:
    static constexpr int map_height = 52;

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

private:
    static ScreenPoint to_screen(GeoPointE7 point, const Viewport& viewport);
    static bool clip_line(ScreenPoint& a, ScreenPoint& b);
    static void draw_position(Framebuffer& framebuffer, ScreenPoint point, float heading_deg);
    static void draw_status(Framebuffer& framebuffer, const NavigationSnapshot& navigation);
};

}  // namespace bikeos
