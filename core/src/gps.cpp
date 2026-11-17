#include "bikeos/gps.hpp"

#include <cmath>

namespace bikeos {

GpsFilter::GpsFilter(const GpsFilterConfig config) : config_(config) {}

std::optional<GpsFix> GpsFilter::accept(const GpsFix& candidate)
{
    if (!candidate.position_valid || !valid_coordinate(candidate.position) ||
        !std::isfinite(candidate.speed_mps) || candidate.speed_mps < 0.0F ||
        candidate.speed_mps > config_.maximum_speed_mps ||
        !std::isfinite(candidate.accuracy_m) || candidate.accuracy_m < 0.0F) {
        return std::nullopt;
    }
    if (candidate.heading_valid &&
        (!std::isfinite(candidate.heading_deg) || candidate.heading_deg < 0.0F ||
         candidate.heading_deg >= 360.0F)) {
        return std::nullopt;
    }
    if (last_fix_) {
        if (candidate.monotonic_timestamp_ms <= last_fix_->monotonic_timestamp_ms ||
            (candidate.utc_timestamp_ms != 0 && last_fix_->utc_timestamp_ms != 0 &&
             candidate.utc_timestamp_ms < last_fix_->utc_timestamp_ms)) {
            return std::nullopt;
        }
        const double elapsed_s =
            static_cast<double>(candidate.monotonic_timestamp_ms - last_fix_->monotonic_timestamp_ms) / 1000.0;
        const double maximum_jump = config_.jump_allowance_m +
                                    elapsed_s * static_cast<double>(config_.maximum_speed_mps) +
                                    candidate.accuracy_m + last_fix_->accuracy_m;
        if (distance_m(last_fix_->position, candidate.position) > maximum_jump) {
            return std::nullopt;
        }
    }
    last_fix_ = candidate;
    return last_fix_;
}

bool GpsFilter::stale(const std::uint64_t now_monotonic_ms) const
{
    return !last_fix_ || now_monotonic_ms < last_fix_->monotonic_timestamp_ms ||
           now_monotonic_ms - last_fix_->monotonic_timestamp_ms > config_.stale_after_ms;
}

const std::optional<GpsFix>& GpsFilter::last_fix() const { return last_fix_; }

void GpsFilter::reset() { last_fix_.reset(); }

}  // namespace bikeos
