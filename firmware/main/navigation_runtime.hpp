#pragma once

#include <cstdint>
#include <span>

#include "bikeos/protocol.hpp"

namespace bikeos {

class NavigationRuntime {
public:
    bool initialize();
    void step(std::uint64_t now_ms);
    // Draws one display frame. On the color TFT this runs several times per
    // second and glides the map between 1 Hz navigation steps; `frame_ms` is a
    // monotonic millisecond clock. No-op for displays redrawn by step().
    void render_frame(std::uint64_t frame_ms);
    void set_phone_connected(bool connected);
    protocol::Status status() const;
    bool consume_reroute_request();
    protocol::ResultCode install_bundle(std::uint32_t bundle_id,
                                        const protocol::TransferBegin& begin,
                                        std::span<const std::uint8_t> map_bytes,
                                        std::span<const std::uint8_t> route_bytes);
    protocol::ResultCode handle_navigation_command(protocol::MessageType command,
                                                   std::span<const std::uint8_t> payload = {});

private:
    struct State;
    State* state_{};
};

}  // namespace bikeos
