#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "bikeos/gps.hpp"
#include "bikeos/navigation.hpp"

namespace bikeos {

enum class SimulationEventType : std::uint8_t {
    normal,
    stop,
    off_route,
    gps_loss,
    jump,
};

struct SimulationEvent {
    float start_progress_m{};
    float end_progress_m{};
    SimulationEventType type{SimulationEventType::normal};
    float magnitude_m{};
};

class GpsSimulator {
public:
    explicit GpsSimulator(std::uint32_t seed = 0xB14E05U);

    bool set_route(const Route& route);
    void set_speed_mps(float speed_mps);
    void set_events(std::vector<SimulationEvent> events);
    void reset();
    std::optional<GpsFix> step(std::uint64_t monotonic_timestamp_ms, float elapsed_seconds = 1.0F);
    float progress_m() const;
    bool finished() const;

private:
    float noise(float amplitude_m);
    GeoPointE7 position_at_progress(float progress_m, float lateral_offset_m) const;
    SimulationEventType active_event(float progress_m, float& magnitude_m) const;

    const Route* route_{};
    std::vector<SimulationEvent> events_;
    float speed_mps_{5.0F};
    float progress_m_{};
    std::uint32_t random_state_{};
    std::uint64_t utc_base_ms_{12ULL * 60ULL * 60ULL * 1000ULL};
};

}  // namespace bikeos
