#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "bikeos/canvas.hpp"
#include "bikeos/geo.hpp"
#include "bikeos/map_data.hpp"
#include "bikeos/navigation.hpp"

namespace bikeos {

enum class ThemeMode : std::uint8_t {
    automatic,
    day,
    night,
};

struct ColorFrameInput {
    std::int64_t utc_epoch_ms{};  // 0 when the GPS has not reported a date and time yet
    ThemeMode theme{ThemeMode::automatic};
    // Monotonic frame time. When set, the rider glides between fixes; 0 draws
    // each snapshot exactly where it is (tests, previews, 1 Hz builds).
    std::uint64_t now_ms{};
};

// Europe/Amsterdam wall-clock time (CET, CEST from the last Sunday of March
// to the last Sunday of October, switching at 01:00 UTC).
std::int64_t amsterdam_local_ms(std::int64_t utc_epoch_ms);
// Sun elevation above the horizon in degrees (NOAA approximation, ~0.5°).
double solar_elevation_deg(std::int64_t utc_epoch_ms, GeoPointE7 position);
bool night_theme(const ColorFrameInput& input, GeoPointE7 position);
// "120 m", "1.2 km", "12 km"
std::string format_distance(float metres);

// Full-color layout for the 320x240 TFT; see docs/screen-design.md.
class ColorMapRenderer {
public:
    static constexpr float cruise_meters_per_pixel = 1.6F;
    static constexpr float turn_meters_per_pixel = 0.9F;
    static constexpr float turn_zoom_distance_m = 80.0F;
    static constexpr float then_line_max_gap_m = 250.0F;
    static constexpr int rider_x = 160;
    static constexpr int rider_y = 172;

    struct Rect {
        int x{};
        int y{};
        int width{};
        int height{};

        bool intersects(const Rect& other) const;
    };

    void render(Canvas565& canvas,
                const MapDataset& map,
                std::span<const GeoPointE7> route,
                const NavigationSnapshot& navigation,
                const ColorFrameInput& input = {});

    float meters_per_pixel() const { return meters_per_pixel_; }
    // Card and chip rectangles from the last frame, for tests and label layout.
    const Rect& card_rect() const { return card_; }

private:
    struct Bounds {
        GeoPointE7 centre;
        float radius_m{};
    };
    void update_bounds(const MapDataset& map);

    // Bounding circles per road and area, rebuilt when the map changes, so
    // off-screen features are skipped before any projection work.
    const MapDataset* bounds_map_{};
    std::size_t bounds_roads_{};
    std::size_t bounds_areas_{};
    std::vector<Bounds> road_bounds_;
    std::vector<Bounds> area_bounds_;

    float meters_per_pixel_{cruise_meters_per_pixel};
    bool zoom_initialized_{};
    std::uint64_t last_frame_ms_{};
    // Motion smoothing between fixes.
    GeoPointE7 from_position_{};
    GeoPointE7 to_position_{};
    bool have_position_{};
    float from_heading_deg_{};
    float to_heading_deg_{};
    float move_heading_target_deg_{};
    bool have_heading_{};
    std::uint64_t move_start_ms_{};
    std::uint32_t move_duration_ms_{};
    std::uint64_t last_fix_ms_{};
    Rect card_{};
};

}  // namespace bikeos
