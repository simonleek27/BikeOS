import CoreBluetooth
import Foundation

@MainActor final class BikeOSBluetooth: NSObject, ObservableObject {
    @Published private(set) var connection = "Disconnected"
    @Published private(set) var status: BikeOSStatus?
    @Published private(set) var lastAck = "No transfer yet"
    @Published private(set) var deviceName = "BikeOS"

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var control: CBCharacteristic?
    private var data: CBCharacteristic?
    private var ack: CBCharacteristic?
    private var statusCharacteristic: CBCharacteristic?
    private var sequence: UInt32 = 1

    override init() { super.init(); central = CBCentralManager(delegate: self, queue: nil) }
    func connect() { guard central.state == .poweredOn else { connection = "Bluetooth unavailable"; return }; connection = "Scanning…"; central.scanForPeripherals(withServices: [CBUUID(string: BikeOSUUID.service)], options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]) }
    func disconnect() { if let peripheral { central.cancelPeripheralConnection(peripheral) } }
    func requestStatus() { sendControl(type: .statusRequest, payload: Data()) }
    func sendDemoRoute() {
        guard control != nil, data != nil else { lastAck = "Connect to BikeOS first"; return }
        guard let mapURL = Bundle.main.url(forResource: "amsterdam_demo", withExtension: "bmap"), let routeURL = Bundle.main.url(forResource: "amsterdam_demo", withExtension: "brte"), let map = try? Data(contentsOf: mapURL), let route = try? Data(contentsOf: routeURL) else { lastAck = "Demo assets are not in the app bundle"; return }
        let bundleID = UInt32(Date().timeIntervalSince1970) ^ 0xB1CE_0005
        let routeID: UInt32 = 2_690_449_409
        var begin = Data(); begin.appendLE(bundleID); begin.appendLE(routeID); begin.appendLE(UInt32(map.count)); begin.appendLE(UInt32(route.count)); begin.appendLE(BikeOSFrame.crc32(map)); begin.appendLE(BikeOSFrame.crc32(route)); begin.appendLE(BikeOSFrame.crc32(map + route))
        let transferID = bundleID; sendControl(type: .transferBegin, payload: begin, transferID: transferID); lastAck = "Sending demo map (\(map.count + route.count) bytes)…"
        var combined = map; combined.append(route)
        // 150 bytes keeps the 20-byte protocol header below the iOS 182-byte no-response write limit.
        for offset in stride(from: 0, to: combined.count, by: 150) { let end = min(offset + 150, combined.count); var chunk = Data(); chunk.appendLE(UInt32(offset)); chunk.append(combined[offset..<end]); let frame = BikeOSFrame(type: .transferChunk, transferID: transferID, sequence: sequence, flags: 0, payload: chunk).encoded(); sequence += 1; peripheral?.writeValue(frame, for: data!, type: .withoutResponse) }
        sendControl(type: .transferCommit, payload: Data(), transferID: transferID); lastAck = "Demo route transfer complete — waiting for ESP32 ACK"
    }
    private func sendControl(type: BikeOSMessageType, payload: Data, transferID: UInt32 = 0) { guard let peripheral, let control else { return }; let frame = BikeOSFrame(type: type, transferID: transferID, sequence: sequence, flags: 0, payload: payload).encoded(); sequence += 1; peripheral.writeValue(frame, for: control, type: .withResponse) }
}

extension BikeOSBluetooth: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) { Task { @MainActor in if central.state == .poweredOn { self.connection = "Ready — tap Connect" } else { self.connection = "Bluetooth unavailable" } } }
    nonisolated func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String : Any], rssi RSSI: NSNumber) { Task { @MainActor in self.peripheral = peripheral; self.deviceName = peripheral.name ?? "BikeOS"; self.connection = "Connecting…"; central.stopScan(); peripheral.delegate = self; central.connect(peripheral) } }
    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) { Task { @MainActor in self.connection = "Connected"; peripheral.discoverServices([CBUUID(string: BikeOSUUID.service)]) } }
    nonisolated func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) { Task { @MainActor in self.connection = "Disconnected"; self.status = nil } }
}

extension BikeOSBluetooth: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) { if let service = peripheral.services?.first { peripheral.discoverCharacteristics(nil, for: service) } }
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        Task { @MainActor in
            for characteristic in service.characteristics ?? [] {
                switch characteristic.uuid.uuidString.uppercased() { case BikeOSUUID.control: control = characteristic; case BikeOSUUID.data: data = characteristic; case BikeOSUUID.acknowledgement: ack = characteristic; peripheral.setNotifyValue(true, for: characteristic); case BikeOSUUID.status: statusCharacteristic = characteristic; peripheral.setNotifyValue(true, for: characteristic); default: break }
            }
            requestStatus()
        }
    }
    nonisolated func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) { guard let value = characteristic.value, let frame = BikeOSFrame.decode(value) else { return }; Task { @MainActor in if let decoded = BikeOSStatus.decode(frame) { status = decoded }; if frame.type == .acknowledgement { lastAck = "ACK received" } } }
}
