#include "bikeos/protocol.hpp"

#include <array>
#include <limits>
#include <type_traits>

namespace bikeos::protocol {
namespace {

constexpr std::array<std::uint8_t, 2> magic{'B', 'K'};

template <typename T>
void append_le(std::vector<std::uint8_t>& output, T value)
{
    using Unsigned = std::make_unsigned_t<T>;
    const Unsigned bits = static_cast<Unsigned>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        output.push_back(static_cast<std::uint8_t>((bits >> (i * 8U)) & 0xFFU));
    }
}

template <typename T>
bool read_le(std::span<const std::uint8_t> input, std::size_t& offset, T& value)
{
    if (offset + sizeof(T) > input.size()) return false;
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned bits = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bits |= static_cast<Unsigned>(input[offset + i]) << (i * 8U);
    }
    value = static_cast<T>(bits);
    offset += sizeof(T);
    return true;
}

bool valid_type(const std::uint8_t value)
{
    return value >= static_cast<std::uint8_t>(MessageType::status_request) &&
           value <= static_cast<std::uint8_t>(MessageType::set_route_eta);
}

}  // namespace

std::uint32_t crc32(const std::span<const std::uint8_t> data)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

std::vector<std::uint8_t> encode_frame(const FrameHeader& header, const std::span<const std::uint8_t> payload)
{
    if (payload.size() > max_frame_payload || !valid_type(static_cast<std::uint8_t>(header.type))) return {};
    std::vector<std::uint8_t> output;
    output.reserve(frame_header_size + payload.size());
    output.insert(output.end(), magic.begin(), magic.end());
    output.push_back(version);
    output.push_back(static_cast<std::uint8_t>(header.type));
    append_le(output, header.transfer_id);
    append_le(output, header.sequence);
    append_le(output, static_cast<std::uint16_t>(payload.size()));
    append_le(output, header.flags);
    append_le(output, crc32(payload));
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

std::vector<std::uint8_t> encode_ble_fragment(const std::uint16_t message_id,
                                              const std::uint16_t total_size,
                                              const std::uint16_t offset,
                                              const std::span<const std::uint8_t> payload)
{
    if (total_size < frame_header_size || total_size > max_frame_size || payload.empty() ||
        offset >= total_size || static_cast<std::size_t>(offset) + payload.size() > total_size ||
        payload.size() > UINT16_MAX - ble_fragment_header_size) {
        return {};
    }
    std::vector<std::uint8_t> output;
    output.reserve(ble_fragment_header_size + payload.size());
    output.push_back(ble_fragment_magic);
    output.push_back(ble_fragment_version);
    append_le(output, message_id);
    append_le(output, total_size);
    append_le(output, offset);
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

bool decode_ble_fragment(const std::span<const std::uint8_t> encoded,
                         BleFragmentHeader& header,
                         std::span<const std::uint8_t>& payload)
{
    if (encoded.size() < ble_fragment_header_size || encoded[0] != ble_fragment_magic ||
        encoded[1] != ble_fragment_version) {
        return false;
    }
    std::size_t offset = 2;
    if (!read_le(encoded, offset, header.message_id) || !read_le(encoded, offset, header.total_size) ||
        !read_le(encoded, offset, header.offset) || header.total_size < frame_header_size ||
        header.total_size > max_frame_size || header.offset >= header.total_size || offset >= encoded.size() ||
        static_cast<std::size_t>(header.offset) + (encoded.size() - offset) > header.total_size) {
        return false;
    }
    payload = encoded.subspan(offset);
    return true;
}

BleFragmentResult BleFragmentReassembler::append(const std::span<const std::uint8_t> fragment,
                                                 std::vector<std::uint8_t>& complete_frame)
{
    complete_frame.clear();
    BleFragmentHeader header;
    std::span<const std::uint8_t> payload;
    if (!decode_ble_fragment(fragment, header, payload)) {
        reset();
        return BleFragmentResult::invalid;
    }
    if (!active_) {
        if (header.offset != 0) return BleFragmentResult::invalid;
        active_ = true;
        message_id_ = header.message_id;
        total_size_ = header.total_size;
        bytes_.clear();
        bytes_.reserve(total_size_);
    }
    if (header.message_id != message_id_ || header.total_size != total_size_ ||
        header.offset != bytes_.size()) {
        reset();
        return BleFragmentResult::invalid;
    }
    bytes_.insert(bytes_.end(), payload.begin(), payload.end());
    if (bytes_.size() < total_size_) return BleFragmentResult::incomplete;
    complete_frame = std::move(bytes_);
    reset();
    return BleFragmentResult::complete;
}

void BleFragmentReassembler::reset()
{
    active_ = false;
    message_id_ = 0;
    total_size_ = 0;
    bytes_.clear();
}

bool decode_frame(const std::span<const std::uint8_t> encoded, DecodedFrame& output)
{
    if (encoded.size() < frame_header_size || encoded[0] != magic[0] || encoded[1] != magic[1] ||
        encoded[2] != version) {
        return false;
    }
    std::size_t offset = 3;
    std::uint8_t type = 0;
    std::uint16_t payload_size = 0;
    FrameHeader header;
    if (!read_le(encoded, offset, type) || !read_le(encoded, offset, header.transfer_id) ||
        !read_le(encoded, offset, header.sequence) || !read_le(encoded, offset, payload_size) ||
        !read_le(encoded, offset, header.flags) || !read_le(encoded, offset, header.payload_crc) ||
        !valid_type(type) || payload_size > max_frame_payload || encoded.size() != frame_header_size + payload_size ||
        crc32(encoded.subspan(frame_header_size)) != header.payload_crc) {
        return false;
    }
    header.type = static_cast<MessageType>(type);
    output = {.header = header, .payload = encoded.subspan(frame_header_size)};
    return true;
}

std::vector<std::uint8_t> encode_transfer_begin(const TransferBegin& begin)
{
    std::vector<std::uint8_t> output;
    output.reserve(28);
    append_le(output, begin.bundle_id);
    append_le(output, begin.route_id);
    append_le(output, begin.map_size);
    append_le(output, begin.route_size);
    append_le(output, begin.map_crc);
    append_le(output, begin.route_crc);
    append_le(output, begin.bundle_crc);
    return output;
}

bool decode_transfer_begin(const std::span<const std::uint8_t> payload, TransferBegin& output)
{
    if (payload.size() != 28) return false;
    std::size_t offset = 0;
    return read_le(payload, offset, output.bundle_id) && read_le(payload, offset, output.route_id) &&
           read_le(payload, offset, output.map_size) && read_le(payload, offset, output.route_size) &&
           read_le(payload, offset, output.map_crc) && read_le(payload, offset, output.route_crc) &&
           read_le(payload, offset, output.bundle_crc) && output.map_size <= max_bundle_size &&
           output.route_size <= max_bundle_size && output.route_size <= max_bundle_size - output.map_size;
}

std::vector<std::uint8_t> encode_transfer_chunk(const std::uint32_t offset,
                                                const std::span<const std::uint8_t> bytes)
{
    if (bytes.empty() || bytes.size() > max_frame_payload - sizeof(offset)) return {};
    std::vector<std::uint8_t> output;
    output.reserve(sizeof(offset) + bytes.size());
    append_le(output, offset);
    output.insert(output.end(), bytes.begin(), bytes.end());
    return output;
}

bool decode_transfer_chunk(const std::span<const std::uint8_t> payload, TransferChunk& output)
{
    if (payload.size() <= sizeof(std::uint32_t)) return false;
    std::size_t offset = 0;
    if (!read_le(payload, offset, output.offset)) return false;
    output.bytes = payload.subspan(offset);
    return output.bytes.size() <= max_frame_payload - sizeof(std::uint32_t);
}

std::vector<std::uint8_t> encode_acknowledgement(const Acknowledgement& acknowledgement)
{
    std::vector<std::uint8_t> output;
    output.reserve(9);
    append_le(output, static_cast<std::uint8_t>(acknowledgement.result));
    append_le(output, acknowledgement.next_offset);
    append_le(output, acknowledgement.bundle_id);
    return output;
}

std::vector<std::uint8_t> encode_navigation_restore(const NavigationRestore& restore)
{
    if (restore.route_id == 0 || static_cast<std::uint8_t>(restore.run_state) >
                                     static_cast<std::uint8_t>(RunState::arrived)) {
        return {};
    }
    std::vector<std::uint8_t> output;
    output.reserve(9);
    append_le(output, restore.route_id);
    append_le(output, restore.progress_cm);
    append_le(output, static_cast<std::uint8_t>(restore.run_state));
    return output;
}

bool decode_navigation_restore(const std::span<const std::uint8_t> payload, NavigationRestore& output)
{
    if (payload.size() != 9) return false;
    std::size_t offset = 0;
    std::uint8_t run_state = 0;
    if (!read_le(payload, offset, output.route_id) || !read_le(payload, offset, output.progress_cm) ||
        !read_le(payload, offset, run_state) || output.route_id == 0 ||
        run_state > static_cast<std::uint8_t>(RunState::arrived)) {
        return false;
    }
    output.run_state = static_cast<RunState>(run_state);
    return true;
}

std::vector<std::uint8_t> encode_route_eta(const RouteEta& eta)
{
    if (eta.route_id == 0 || eta.eta_minutes_local >= 1440) return {};
    std::vector<std::uint8_t> output;
    output.reserve(6);
    append_le(output, eta.route_id);
    append_le(output, eta.eta_minutes_local);
    return output;
}

bool decode_route_eta(const std::span<const std::uint8_t> payload, RouteEta& output)
{
    if (payload.size() != 6) return false;
    std::size_t offset = 0;
    return read_le(payload, offset, output.route_id) && read_le(payload, offset, output.eta_minutes_local) &&
           output.route_id != 0 && output.eta_minutes_local < 1440;
}

bool decode_acknowledgement(const std::span<const std::uint8_t> payload, Acknowledgement& output)
{
    if (payload.size() != 9 || payload[0] > static_cast<std::uint8_t>(ResultCode::unauthorized)) return false;
    std::size_t offset = 0;
    std::uint8_t result = 0;
    if (!read_le(payload, offset, result) || !read_le(payload, offset, output.next_offset) ||
        !read_le(payload, offset, output.bundle_id)) return false;
    output.result = static_cast<ResultCode>(result);
    return true;
}

std::vector<std::uint8_t> encode_status(const Status& status)
{
    std::vector<std::uint8_t> output;
    output.reserve(36);
    append_le(output, status.protocol_version);
    append_le(output, static_cast<std::uint8_t>(status.navigation_state));
    append_le(output, static_cast<std::uint8_t>(status.gps_source));
    append_le(output, status.flags);
    append_le(output, status.latitude_e7);
    append_le(output, status.longitude_e7);
    append_le(output, status.active_route_id);
    append_le(output, status.route_remaining_m);
    append_le(output, status.next_maneuver_m);
    append_le(output, status.free_heap);
    append_le(output, status.heading_deg_cdeg);
    append_le(output, status.speed_cms);
    append_le(output, status.matched_point_index);
    append_le(output, static_cast<std::uint8_t>(status.run_state));
    append_le(output, status.reserved);
    return output;
}

bool decode_status(const std::span<const std::uint8_t> payload, Status& output)
{
    if ((payload.size() != 28 && payload.size() != 36) || payload[0] != version || payload[1] > static_cast<std::uint8_t>(NavigationState::error) ||
        payload[2] > static_cast<std::uint8_t>(GpsSource::iphone_fallback)) {
        return false;
    }
    std::size_t offset = 0;
    std::uint8_t navigation_state = 0;
    std::uint8_t gps_source = 0;
    if (!read_le(payload, offset, output.protocol_version) || !read_le(payload, offset, navigation_state) ||
        !read_le(payload, offset, gps_source) || !read_le(payload, offset, output.flags) ||
        !read_le(payload, offset, output.latitude_e7) || !read_le(payload, offset, output.longitude_e7) ||
        !read_le(payload, offset, output.active_route_id) || !read_le(payload, offset, output.route_remaining_m) ||
        !read_le(payload, offset, output.next_maneuver_m) || !read_le(payload, offset, output.free_heap)) {
        return false;
    }
    if (payload.size() == 36 && (!read_le(payload, offset, output.heading_deg_cdeg) ||
                                 !read_le(payload, offset, output.speed_cms) ||
                                 !read_le(payload, offset, output.matched_point_index))) {
        return false;
    }
    if (payload.size() == 36) {
        std::uint8_t run_state = 0;
        if (!read_le(payload, offset, run_state) || !read_le(payload, offset, output.reserved) ||
            run_state > static_cast<std::uint8_t>(RunState::arrived)) return false;
        output.run_state = static_cast<RunState>(run_state);
    }
    output.navigation_state = static_cast<NavigationState>(navigation_state);
    output.gps_source = static_cast<GpsSource>(gps_source);
    return true;
}

}  // namespace bikeos::protocol
