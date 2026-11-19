#include "ble_peripheral.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_att.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

namespace bikeos {
namespace {

constexpr char kTag[] = "ble";
constexpr char kDeviceName[] = "BikeOS";

#define BIKEOS_UUID128(last) \
    ble_uuid128_t{{BLE_UUID_TYPE_128}, {0x53, 0x4F, 0x45, 0x4B, 0x49, 0x42, 0x53, 0x9B, \
                  0x10, 0x4F, 0x9A, 0x9C, last, 0x00, 0xD3, 0xA9}}
const ble_uuid128_t service_uuid_value = BIKEOS_UUID128(0x00);
const ble_uuid128_t device_info_uuid_value = BIKEOS_UUID128(0x01);
const ble_uuid128_t status_uuid_value = BIKEOS_UUID128(0x02);
const ble_uuid128_t control_uuid_value = BIKEOS_UUID128(0x03);
const ble_uuid128_t data_uuid_value = BIKEOS_UUID128(0x04);
const ble_uuid128_t ack_uuid_value = BIKEOS_UUID128(0x05);
const ble_uuid128_t event_uuid_value = BIKEOS_UUID128(0x06);
const ble_uuid_t* service_uuid = &service_uuid_value.u;
const ble_uuid_t* device_info_uuid = &device_info_uuid_value.u;
const ble_uuid_t* status_uuid = &status_uuid_value.u;
const ble_uuid_t* control_uuid = &control_uuid_value.u;
const ble_uuid_t* data_uuid = &data_uuid_value.u;
const ble_uuid_t* ack_uuid = &ack_uuid_value.u;
const ble_uuid_t* event_uuid = &event_uuid_value.u;

BlePeripheral* g_instance = nullptr;
std::uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
std::uint16_t g_status_handle = 0;
std::uint16_t g_ack_handle = 0;
std::uint16_t g_event_handle = 0;
bool g_status_notify = false;
bool g_ack_notify = false;
bool g_event_notify = false;

struct TransferState {
    bool active{};
    std::uint32_t transfer_id{};
    protocol::TransferBegin begin{};
    std::vector<std::uint8_t> bundle;
    std::uint32_t next_offset{};
};

TransferState g_transfer;

int append_bytes(struct os_mbuf* om, const std::span<const std::uint8_t> bytes)
{
    return os_mbuf_append(om, bytes.data(), bytes.size()) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

struct os_mbuf* make_mbuf(const std::span<const std::uint8_t> bytes)
{
    if (bytes.size() > UINT16_MAX) return nullptr;
    struct os_mbuf* om = os_msys_get_pkthdr(static_cast<uint16_t>(bytes.size()), 0);
    if (om == nullptr || os_mbuf_append(om, bytes.data(), bytes.size()) != 0) {
        if (om != nullptr) os_mbuf_free_chain(om);
        return nullptr;
    }
    return om;
}

bool flatten(const struct os_mbuf* om, std::vector<std::uint8_t>& output)
{
    const std::size_t length = OS_MBUF_PKTLEN(om);
    if (length > protocol::frame_header_size + protocol::max_frame_payload) return false;
    output.resize(length);
    return ble_hs_mbuf_to_flat(om, output.data(), output.size(), nullptr) == 0;
}

void send_ack(const std::uint32_t transfer_id,
              const std::uint32_t sequence,
              const protocol::Acknowledgement& acknowledgement)
{
    if (!g_ack_notify || g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    const auto payload = protocol::encode_acknowledgement(acknowledgement);
    const auto frame = protocol::encode_frame({.type = protocol::MessageType::acknowledgement,
                                               .transfer_id = transfer_id,
                                               .sequence = sequence},
                                              payload);
    if (frame.empty()) return;
    struct os_mbuf* om = make_mbuf(frame);
    if (om == nullptr) return;
    if (ble_gatts_notify_custom(g_conn_handle, g_ack_handle, om) != 0) {
        os_mbuf_free_chain(om);
    }
}

int access_device_info(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    constexpr char info[] =
        "BikeOS firmware=0.3.0 protocol=1 capabilities=sim-gps,bmap-v4,brte-v1,nav-controls";
    return os_mbuf_append(ctxt->om, info, sizeof(info) - 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int access_status(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR || g_instance == nullptr) return BLE_ATT_ERR_READ_NOT_PERMITTED;
    const protocol::Status status = g_instance->read_status();
    const auto payload = protocol::encode_status(status);
    const auto frame = protocol::encode_frame({.type = protocol::MessageType::status}, payload);
    return append_bytes(ctxt->om, frame);
}

int access_control(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || g_instance == nullptr) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    std::vector<std::uint8_t> bytes;
    if (!flatten(ctxt->om, bytes)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    protocol::DecodedFrame frame;
    if (!protocol::decode_frame(bytes, frame)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    protocol::Acknowledgement acknowledgement{.result = protocol::ResultCode::invalid_frame};
    if (frame.header.type == protocol::MessageType::status_request) {
        acknowledgement.result = protocol::ResultCode::ok;
        send_ack(frame.header.transfer_id, frame.header.sequence, acknowledgement);
        return 0;
    }
    const bool is_navigation_command = frame.header.type == protocol::MessageType::start_navigation ||
                                       frame.header.type == protocol::MessageType::cancel_navigation ||
                                       frame.header.type == protocol::MessageType::pause_navigation ||
                                       frame.header.type == protocol::MessageType::resume_navigation ||
                                       frame.header.type == protocol::MessageType::restart_navigation ||
                                       frame.header.type == protocol::MessageType::stop_navigation ||
                                       frame.header.type == protocol::MessageType::trigger_off_route;
    if (is_navigation_command) {
        acknowledgement.result = g_transfer.active ? protocol::ResultCode::busy
                                                    : g_instance->handle_navigation_command(frame.header.type);
        send_ack(frame.header.transfer_id, frame.header.sequence, acknowledgement);
        return 0;
    }
    if (frame.header.type == protocol::MessageType::transfer_begin) {
        protocol::TransferBegin begin{};
        if (g_transfer.active) {
            acknowledgement.result = protocol::ResultCode::busy;
        } else if (!protocol::decode_transfer_begin(frame.payload, begin) || begin.bundle_id == 0 ||
                   begin.route_id == 0 || begin.map_size == 0 || begin.route_size == 0 ||
                   begin.map_size > protocol::max_bundle_size || begin.route_size > protocol::max_bundle_size ||
                   begin.route_size > protocol::max_bundle_size - begin.map_size) {
            acknowledgement.result = protocol::ResultCode::invalid_transfer;
        } else {
            g_transfer = {};
            g_transfer.active = true;
            g_transfer.transfer_id = frame.header.transfer_id;
            g_transfer.begin = begin;
            g_transfer.bundle.resize(static_cast<std::size_t>(begin.map_size) + begin.route_size);
            acknowledgement.result = protocol::ResultCode::accepted;
        }
        acknowledgement.bundle_id = begin.bundle_id;
        send_ack(frame.header.transfer_id, frame.header.sequence, acknowledgement);
        return 0;
    }
    if (frame.header.type == protocol::MessageType::transfer_commit) {
        if (!g_transfer.active || frame.header.transfer_id != g_transfer.transfer_id ||
            g_transfer.next_offset != g_transfer.bundle.size() ||
            protocol::crc32(g_transfer.bundle) != g_transfer.begin.bundle_crc ||
            protocol::crc32(std::span<const std::uint8_t>(g_transfer.bundle).first(g_transfer.begin.map_size)) !=
                g_transfer.begin.map_crc ||
            protocol::crc32(std::span<const std::uint8_t>(g_transfer.bundle).subspan(g_transfer.begin.map_size)) !=
                g_transfer.begin.route_crc) {
            acknowledgement.result = protocol::ResultCode::crc_mismatch;
        } else {
            const auto map_bytes = std::span<const std::uint8_t>(g_transfer.bundle).first(g_transfer.begin.map_size);
            const auto route_bytes = std::span<const std::uint8_t>(g_transfer.bundle).subspan(g_transfer.begin.map_size);
            acknowledgement.result = g_instance->handle_bundle(g_transfer.begin.bundle_id,
                                                               g_transfer.begin,
                                                               map_bytes,
                                                               route_bytes);
            if (acknowledgement.result == protocol::ResultCode::ok) g_transfer.active = false;
        }
        acknowledgement.next_offset = g_transfer.next_offset;
        acknowledgement.bundle_id = g_transfer.begin.bundle_id;
        send_ack(frame.header.transfer_id, frame.header.sequence, acknowledgement);
        return 0;
    }
    if (frame.header.type == protocol::MessageType::transfer_cancel) {
        g_transfer = {};
        acknowledgement.result = protocol::ResultCode::cancelled;
        send_ack(frame.header.transfer_id, frame.header.sequence, acknowledgement);
        return 0;
    }
    acknowledgement.result = protocol::ResultCode::invalid_frame;
    send_ack(frame.header.transfer_id, frame.header.sequence, acknowledgement);
    return 0;
}

int access_data(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    std::vector<std::uint8_t> bytes;
    if (!flatten(ctxt->om, bytes)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    protocol::DecodedFrame frame;
    protocol::TransferChunk chunk;
    const bool decoded = protocol::decode_frame(bytes, frame);
    if (!decoded || frame.header.type != protocol::MessageType::transfer_chunk ||
        !protocol::decode_transfer_chunk(frame.payload, chunk) || !g_transfer.active ||
        frame.header.transfer_id != g_transfer.transfer_id || chunk.offset != g_transfer.next_offset ||
        chunk.offset + chunk.bytes.size() > g_transfer.bundle.size()) {
        if (g_transfer.active && decoded) {
            send_ack(frame.header.transfer_id, frame.header.sequence,
                     {.result = protocol::ResultCode::invalid_offset,
                      .next_offset = g_transfer.next_offset,
                      .bundle_id = g_transfer.begin.bundle_id});
        }
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    std::copy(chunk.bytes.begin(), chunk.bytes.end(), g_transfer.bundle.begin() + chunk.offset);
    g_transfer.next_offset += static_cast<std::uint32_t>(chunk.bytes.size());
    send_ack(frame.header.transfer_id, frame.header.sequence,
             {.result = protocol::ResultCode::accepted,
              .next_offset = g_transfer.next_offset,
              .bundle_id = g_transfer.begin.bundle_id});
    return 0;
}

int access_notify_only(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    return ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR ? BLE_ATT_ERR_READ_NOT_PERMITTED
                                                   : BLE_ATT_ERR_WRITE_NOT_PERMITTED;
}

const ble_gatt_chr_def characteristics[] = {
    {.uuid = device_info_uuid, .access_cb = access_device_info, .flags = BLE_GATT_CHR_F_READ},
    {.uuid = status_uuid,
     .access_cb = access_status,
     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
     .val_handle = &g_status_handle},
    {.uuid = control_uuid,
     .access_cb = access_control,
     .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
    {.uuid = data_uuid,
     .access_cb = access_data,
     .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
    {.uuid = ack_uuid,
     .access_cb = access_notify_only,
     .flags = BLE_GATT_CHR_F_NOTIFY,
     .val_handle = &g_ack_handle},
    {.uuid = event_uuid,
     .access_cb = access_notify_only,
     .flags = BLE_GATT_CHR_F_NOTIFY,
     .val_handle = &g_event_handle},
    {},
};

const ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = service_uuid, .characteristics = characteristics},
    {},
};

void advertise();

int gap_event(ble_gap_event* event, void*)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                g_conn_handle = event->connect.conn_handle;
                if (g_instance != nullptr) g_instance->connection_changed(true);
                ESP_LOGI(kTag, "BLE connected handle=%u", g_conn_handle);
            } else {
                advertise();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(kTag, "BLE disconnected reason=%d", event->disconnect.reason);
            g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            g_status_notify = g_ack_notify = g_event_notify = false;
            g_transfer = {};
            if (g_instance != nullptr) g_instance->connection_changed(false);
            advertise();
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == g_status_handle) g_status_notify = event->subscribe.cur_notify;
            if (event->subscribe.attr_handle == g_ack_handle) g_ack_notify = event->subscribe.cur_notify;
            if (event->subscribe.attr_handle == g_event_handle) g_event_notify = event->subscribe.cur_notify;
            break;
        default:
            break;
    }
    return 0;
}

void on_sync()
{
    std::uint8_t address_type = 0;
    if (ble_hs_id_infer_auto(0, &address_type) != 0) return;
    advertise();
}

void host_task(void*)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void advertise()
{
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = reinterpret_cast<std::uint8_t*>(const_cast<char*>(kDeviceName));
    fields.name_len = sizeof(kDeviceName) - 1;
    fields.name_is_complete = 1;
    fields.uuids128 = const_cast<ble_uuid128_t*>(&service_uuid_value);
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields) != 0) return;
    ble_gap_adv_params params{};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    std::uint8_t address_type = 0;
    if (ble_hs_id_infer_auto(0, &address_type) == 0) {
        ble_gap_adv_start(address_type, nullptr, BLE_HS_FOREVER, &params, gap_event, nullptr);
        ESP_LOGI(kTag, "Advertising as %s", kDeviceName);
    }
}

}  // namespace

bool BlePeripheral::initialize(StatusProvider status_provider,
                               BundleHandler bundle_handler,
                               ConnectionHandler connection_handler,
                               CommandHandler command_handler)
{
    status_provider_ = std::move(status_provider);
    bundle_handler_ = std::move(bundle_handler);
    connection_handler_ = std::move(connection_handler);
    command_handler_ = std::move(command_handler);
    g_instance = this;
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() != ESP_OK) return false;
        nvs_result = nvs_flash_init();
    }
    if (nvs_result != ESP_OK || nimble_port_init() != ESP_OK) return false;
    // The transfer-begin frame carries the route metadata in one write. Keep
    // the ATT MTU large enough for that frame and for the negotiated iPhone
    // data chunks; CoreBluetooth will still cap each write to the link's
    // negotiated maximum.
    if (ble_att_set_preferred_mtu(256) != 0) return false;
    ble_hs_cfg.sync_cb = on_sync;
    if (ble_svc_gap_device_name_set(kDeviceName) != 0 || ble_gatts_count_cfg(services) != 0 ||
        ble_gatts_add_svcs(services) != 0) {
        return false;
    }
    nimble_port_freertos_init(host_task);
    ESP_LOGI(kTag, "BLE protocol v%u initialized", protocol::version);
    return true;
}

void BlePeripheral::connection_changed(const bool connected)
{
    if (connection_handler_) connection_handler_(connected);
}

protocol::Status BlePeripheral::read_status() const
{
    return status_provider_ ? status_provider_() : protocol::Status{};
}

protocol::ResultCode BlePeripheral::handle_bundle(const std::uint32_t bundle_id,
                                                   const protocol::TransferBegin& begin,
                                                   const std::span<const std::uint8_t> map_bytes,
                                                   const std::span<const std::uint8_t> route_bytes) const
{
    return bundle_handler_ ? bundle_handler_(bundle_id, begin, map_bytes, route_bytes)
                           : protocol::ResultCode::invalid_transfer;
}

protocol::ResultCode BlePeripheral::handle_navigation_command(const protocol::MessageType command) const
{
    return command_handler_ ? command_handler_(command) : protocol::ResultCode::unauthorized;
}

void BlePeripheral::notify_status(const protocol::Status& status)
{
    if (!g_status_notify || g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    const auto payload = protocol::encode_status(status);
    const auto frame = protocol::encode_frame({.type = protocol::MessageType::status}, payload);
    struct os_mbuf* om = make_mbuf(frame);
    if (om != nullptr && ble_gatts_notify_custom(g_conn_handle, g_status_handle, om) != 0) os_mbuf_free_chain(om);
}

void BlePeripheral::notify_reroute(const protocol::Status& status)
{
    if (!g_event_notify || g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    const auto payload = protocol::encode_status(status);
    const auto frame = protocol::encode_frame({.type = protocol::MessageType::reroute_request}, payload);
    struct os_mbuf* om = make_mbuf(frame);
    if (om != nullptr && ble_gatts_notify_custom(g_conn_handle, g_event_handle, om) != 0) os_mbuf_free_chain(om);
}

}  // namespace bikeos
