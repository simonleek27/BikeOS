#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace bikeos::protocol {

constexpr std::uint8_t version = 1;
constexpr std::size_t frame_header_size = 20;
constexpr std::size_t max_frame_payload = 480;
constexpr std::size_t max_bundle_size = 512 * 1024;

// BLE ATT writes can be as small as 20 bytes. Frames remain unchanged on the
// wire when they fit, but larger frames use this transport-only envelope.
constexpr std::uint8_t ble_fragment_magic = 0xF7;
constexpr std::uint8_t ble_fragment_version = 1;
constexpr std::size_t ble_fragment_header_size = 8;
constexpr std::size_t max_frame_size = frame_header_size + max_frame_payload;

// Stable UUIDs. The service UUID is also included in the advertising payload.
inline constexpr char service_uuid[] = "A9D30000-9C9A-4F10-9B53-42494B454F53";
inline constexpr char device_info_uuid[] = "A9D30001-9C9A-4F10-9B53-42494B454F53";
inline constexpr char status_uuid[] = "A9D30002-9C9A-4F10-9B53-42494B454F53";
inline constexpr char control_uuid[] = "A9D30003-9C9A-4F10-9B53-42494B454F53";
inline constexpr char data_uuid[] = "A9D30004-9C9A-4F10-9B53-42494B454F53";
inline constexpr char acknowledgement_uuid[] = "A9D30005-9C9A-4F10-9B53-42494B454F53";
inline constexpr char event_uuid[] = "A9D30006-9C9A-4F10-9B53-42494B454F53";

enum class MessageType : std::uint8_t {
    status_request = 1,
    transfer_begin = 2,
    transfer_chunk = 3,
    transfer_commit = 4,
    transfer_cancel = 5,
    start_navigation = 6,
    cancel_navigation = 7,
    acknowledgement = 8,
    error = 9,
    status = 10,
    reroute_request = 11,
    pause_navigation = 12,
    resume_navigation = 13,
    restart_navigation = 14,
    stop_navigation = 15,
    trigger_off_route = 16,
};

enum class ResultCode : std::uint8_t {
    ok = 0,
    accepted = 1,
    invalid_frame = 2,
    unsupported_version = 3,
    busy = 4,
    invalid_transfer = 5,
    invalid_offset = 6,
    crc_mismatch = 7,
    invalid_bundle = 8,
    cancelled = 9,
    unauthorized = 10,
};

enum class NavigationState : std::uint8_t {
    no_route = 0,
    waiting_for_gps = 1,
    navigating = 2,
    gps_lost = 3,
    off_route = 4,
    rerouting = 5,
    phone_disconnected = 6,
    destination_reached = 7,
    error = 8,
};

enum class RunState : std::uint8_t {
    not_started = 0,
    running = 1,
    paused = 2,
    stopped = 3,
    arrived = 4,
};

enum class GpsSource : std::uint8_t {
    none = 0,
    simulated = 1,
    uart = 2,
    iphone_fallback = 3,
};

struct FrameHeader {
    MessageType type{MessageType::error};
    std::uint32_t transfer_id{};
    std::uint32_t sequence{};
    std::uint16_t flags{};
    std::uint32_t payload_crc{};
};

struct DecodedFrame {
    FrameHeader header;
    std::span<const std::uint8_t> payload;
};

struct TransferBegin {
    std::uint32_t bundle_id{};
    std::uint32_t route_id{};
    std::uint32_t map_size{};
    std::uint32_t route_size{};
    std::uint32_t map_crc{};
    std::uint32_t route_crc{};
    std::uint32_t bundle_crc{};
};

struct TransferChunk {
    std::uint32_t offset{};
    std::span<const std::uint8_t> bytes;
};

struct Acknowledgement {
    ResultCode result{ResultCode::invalid_frame};
    std::uint32_t next_offset{};
    std::uint32_t bundle_id{};
};

struct BleFragmentHeader {
    std::uint16_t message_id{};
    std::uint16_t total_size{};
    std::uint16_t offset{};
};

enum class BleFragmentResult : std::uint8_t { invalid, incomplete, complete };

class BleFragmentReassembler {
public:
    BleFragmentResult append(std::span<const std::uint8_t> fragment,
                             std::vector<std::uint8_t>& complete_frame);
    void reset();

private:
    bool active_{};
    std::uint16_t message_id_{};
    std::uint16_t total_size_{};
    std::vector<std::uint8_t> bytes_;
};

struct Status {
    std::uint8_t protocol_version{version};
    NavigationState navigation_state{NavigationState::no_route};
    GpsSource gps_source{GpsSource::none};
    std::uint8_t flags{};  // bit 0: GPS valid, bit 1: phone connected, bit 2: display available.
    std::int32_t latitude_e7{};
    std::int32_t longitude_e7{};
    std::uint32_t active_route_id{};
    std::uint32_t route_remaining_m{};
    std::uint32_t next_maneuver_m{};
    std::uint32_t free_heap{};
    // Optional v1 extension (bytes 28-35). Older readers may consume the
    // original 28-byte prefix; zero means that a value is unavailable.
    std::uint16_t heading_deg_cdeg{};
    std::uint16_t speed_cms{};
    std::uint16_t matched_point_index{};
    RunState run_state{RunState::not_started};
    std::uint8_t reserved{};
};

std::uint32_t crc32(std::span<const std::uint8_t> data);
std::vector<std::uint8_t> encode_frame(const FrameHeader& header, std::span<const std::uint8_t> payload);
bool decode_frame(std::span<const std::uint8_t> encoded, DecodedFrame& output);

std::vector<std::uint8_t> encode_ble_fragment(std::uint16_t message_id,
                                              std::uint16_t total_size,
                                              std::uint16_t offset,
                                              std::span<const std::uint8_t> payload);
bool decode_ble_fragment(std::span<const std::uint8_t> encoded,
                         BleFragmentHeader& header,
                         std::span<const std::uint8_t>& payload);

std::vector<std::uint8_t> encode_transfer_begin(const TransferBegin& begin);
bool decode_transfer_begin(std::span<const std::uint8_t> payload, TransferBegin& output);
std::vector<std::uint8_t> encode_transfer_chunk(std::uint32_t offset, std::span<const std::uint8_t> bytes);
bool decode_transfer_chunk(std::span<const std::uint8_t> payload, TransferChunk& output);
std::vector<std::uint8_t> encode_acknowledgement(const Acknowledgement& acknowledgement);
bool decode_acknowledgement(std::span<const std::uint8_t> payload, Acknowledgement& output);
std::vector<std::uint8_t> encode_status(const Status& status);
bool decode_status(std::span<const std::uint8_t> payload, Status& output);

}  // namespace bikeos::protocol
