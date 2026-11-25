#pragma once

#include <cstdint>
#include <functional>
#include <span>

#include "bikeos/protocol.hpp"

namespace bikeos {

class BlePeripheral {
public:
    using StatusProvider = std::function<protocol::Status()>;
    using BundleHandler = std::function<protocol::ResultCode(std::uint32_t,
                                                             const protocol::TransferBegin&,
                                                             std::span<const std::uint8_t>,
                                                             std::span<const std::uint8_t>)>;
    using ConnectionHandler = std::function<void(bool)>;
    using CommandHandler = std::function<protocol::ResultCode(protocol::MessageType,
                                                               std::span<const std::uint8_t>)>;

    bool initialize(StatusProvider status_provider,
                    BundleHandler bundle_handler,
                    ConnectionHandler connection_handler = {},
                    CommandHandler command_handler = {});
    void notify_status(const protocol::Status& status);
    void notify_reroute(const protocol::Status& status);
    protocol::Status read_status() const;
    protocol::ResultCode handle_bundle(std::uint32_t bundle_id,
                                       const protocol::TransferBegin& begin,
                                       std::span<const std::uint8_t> map_bytes,
                                       std::span<const std::uint8_t> route_bytes) const;
    protocol::ResultCode handle_navigation_command(protocol::MessageType command,
                                                   std::span<const std::uint8_t> payload = {}) const;
    void connection_changed(bool connected);

private:
    StatusProvider status_provider_;
    BundleHandler bundle_handler_;
    ConnectionHandler connection_handler_;
    CommandHandler command_handler_;
};

}  // namespace bikeos
