#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "bikeos/gps.hpp"

namespace bikeos {

class NmeaParser {
public:
    std::optional<GpsFix> parse(std::string_view sentence, std::uint64_t monotonic_timestamp_ms);
    static bool checksum_valid(std::string_view sentence);

private:
    std::optional<GpsFix> last_rmc_;
    std::optional<std::int64_t> utc_day_ms_;  // midnight UTC of the last RMC date
};

}  // namespace bikeos
