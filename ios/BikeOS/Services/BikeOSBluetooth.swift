import CoreBluetooth
import Foundation

@MainActor final class BikeOSBluetooth: NSObject, ObservableObject {
    @Published private(set) var connection = "Disconnected"
    @Published private(set) var status: BikeOSStatus?
    @Published private(set) var lastAck = "No transfer yet"
    @Published private(set) var deviceName = "BikeOS"

    private enum TransferPhase { case idle, waitingForBeginAck, sending, verifying, activated, failed }
    private var phase: TransferPhase = .idle
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var control: CBCharacteristic?
    private var data: CBCharacteristic?
    private var sequence: UInt32 = 1
    private var transferID: UInt32 = 0
    private var bundleID: UInt32 = 0
    private var routeID: UInt32 = 0
    private var transferBytes = Data()
    private var nextOffset = 0
    private var commitSent = false

    override init() { super.init(); central = CBCentralManager(delegate: self, queue: nil) }
    func connect() { guard central.state == .poweredOn else { connection = "Bluetooth unavailable"; return }; connection = "Scanning…"; central.scanForPeripherals(withServices: [CBUUID(string: BikeOSUUID.service)], options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]) }
    func disconnect() { if let peripheral { central.cancelPeripheralConnection(peripheral) } }
    func requestStatus() { sendControl(type: .statusRequest, payload: Data()) }

    func sendDemoRoute() {
        guard control != nil, data != nil else { lastAck = "Connect to BikeOS first"; return }
        guard let mapURL = Bundle.main.url(forResource: "amsterdam_demo", withExtension: "bmap"), let routeURL = Bundle.main.url(forResource: "amsterdam_demo", withExtension: "brte"), let map = try? Data(contentsOf: mapURL), let route = try? Data(contentsOf: routeURL) else { lastAck = "Demo assets are not in the app bundle"; return }
        guard let decodedRoute = try? BikeOSRouteDecoder.decode(route) else { lastAck = "Bundled route failed validation"; return }
        bundleID = UInt32(Date().timeIntervalSince1970) ^ 0xB1CE_0005; routeID = decodedRoute.routeID; transferID = bundleID; transferBytes = map + route; nextOffset = 0; commitSent = false; phase = .waitingForBeginAck
        var begin = Data(); begin.appendLE(bundleID); begin.appendLE(routeID); begin.appendLE(UInt32(map.count)); begin.appendLE(UInt32(route.count)); begin.appendLE(BikeOSFrame.crc32(map)); begin.appendLE(BikeOSFrame.crc32(route)); begin.appendLE(BikeOSFrame.crc32(transferBytes))
        lastAck = "Preparing demo route (0%)"; sendControl(type: .transferBegin, payload: begin, transferID: transferID)
    }

    private func sendNextChunk() {
        guard phase == .sending, let peripheral, let data, nextOffset < transferBytes.count else { return }
        let end = min(nextOffset + 150, transferBytes.count); var chunk = Data(); chunk.appendLE(UInt32(nextOffset)); chunk.append(transferBytes[nextOffset..<end]); let frame = BikeOSFrame(type: .transferChunk, transferID: transferID, sequence: sequence, flags: 0, payload: chunk).encoded(); sequence += 1
        peripheral.writeValue(frame, for: data, type: .withoutResponse)
        lastAck = "Sending demo route (\(Int(Double(end) / Double(transferBytes.count) * 100))%)"
    }

    private func sendCommit() { guard !commitSent else { return }; commitSent = true; phase = .verifying; lastAck = "Verifying route on ESP32…"; sendControl(type: .transferCommit, payload: Data(), transferID: transferID) }
    private func sendControl(type: BikeOSMessageType, payload: Data, transferID: UInt32 = 0) { guard let peripheral, let control else { return }; let frame = BikeOSFrame(type: type, transferID: transferID, sequence: sequence, flags: 0, payload: payload).encoded(); sequence += 1; peripheral.writeValue(frame, for: control, type: .withResponse) }

    private func handleAcknowledgement(_ frame: BikeOSFrame) {
        // A status request also produces an ACK, but its transfer ID is zero.
        // Only the active route transfer may advance the transfer state machine.
        guard frame.transferID == transferID, let acknowledgement = BikeOSAcknowledgement.decode(frame) else { return }
        switch phase {
        case .waitingForBeginAck where acknowledgement.result == .accepted:
            phase = .sending; nextOffset = 0; sendNextChunk()
        case .sending where acknowledgement.result == .accepted:
            guard acknowledgement.nextOffset >= UInt32(nextOffset), acknowledgement.nextOffset <= UInt32(transferBytes.count) else { phase = .failed; lastAck = "ESP32 rejected chunk offset"; return }
            nextOffset = Int(acknowledgement.nextOffset); if nextOffset == transferBytes.count { sendCommit() } else { sendNextChunk() }
        case .verifying where acknowledgement.result == .ok:
            lastAck = "Route verified; waiting for active status…"; requestStatus()
        case .waitingForBeginAck, .sending, .verifying:
            phase = .failed; lastAck = "ESP32 transfer error: \(acknowledgement.result)"
        default: break
        }
    }
}

extension BikeOSBluetooth: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) { Task { @MainActor in self.connection = central.state == .poweredOn ? "Ready — tap Connect" : "Bluetooth unavailable" } }
    nonisolated func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String : Any], rssi RSSI: NSNumber) { Task { @MainActor in self.peripheral = peripheral; self.deviceName = peripheral.name ?? "BikeOS"; self.connection = "Connecting…"; central.stopScan(); peripheral.delegate = self; central.connect(peripheral) } }
    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) { Task { @MainActor in self.connection = "Connected"; peripheral.discoverServices([CBUUID(string: BikeOSUUID.service)]) } }
    nonisolated func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) { Task { @MainActor in self.connection = "Disconnected"; self.status = nil; self.phase = .idle } }
}

extension BikeOSBluetooth: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) { if let service = peripheral.services?.first { peripheral.discoverCharacteristics(nil, for: service) } }
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        Task { @MainActor in
            for characteristic in service.characteristics ?? [] { switch characteristic.uuid.uuidString.uppercased() { case BikeOSUUID.control: control = characteristic; case BikeOSUUID.data: data = characteristic; case BikeOSUUID.acknowledgement: peripheral.setNotifyValue(true, for: characteristic); case BikeOSUUID.status: peripheral.setNotifyValue(true, for: characteristic); default: break } }
            requestStatus()
        }
    }
    nonisolated func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) { guard let value = characteristic.value, let frame = BikeOSFrame.decode(value) else { return }; Task { @MainActor in if frame.type == .acknowledgement { handleAcknowledgement(frame); return }; if let decoded = BikeOSStatus.decode(frame) { status = decoded; if phase == .verifying && decoded.routeID == routeID { phase = .activated; lastAck = "Activated route \(routeID) on ESP32" } } } }
}
