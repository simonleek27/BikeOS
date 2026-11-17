#pragma once

#include <cstdint>
#include <span>

#include "bikeos/protocol.hpp"

namespace bikeos {

class NavigationRuntime {
public:
    bool initialize();
    void step(std::uint64_t now_ms);
    void set_phone_connected(bool connected);
    protocol::Status status() const;
    bool consume_reroute_request();
    protocol::ResultCode install_bundle(std::uint32_t bundle_id,
                                        const protocol::TransferBegin& begin,
                                        std::span<const std::uint8_t> map_bytes,
                                        std::span<const std::uint8_t> route_bytes);

private:
    struct State;
    State* state_{};
};

}  // namespace bikeos
