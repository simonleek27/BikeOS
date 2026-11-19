import CoreBluetooth
import CoreLocation
import Combine
import Foundation

@MainActor final class BikeOSBluetooth: NSObject, ObservableObject {
    @Published private(set) var connection = "Disconnected"
    @Published private(set) var status: BikeOSStatus?
    @Published private(set) var lastAck = "No transfer yet"
    @Published private(set) var deviceName = "BikeOS"
    @Published private(set) var transferProgress: Double?
    @Published private(set) var liveCoordinate: CLLocationCoordinate2D?
    @Published private(set) var liveHeading: Double?

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
    private var pendingCommands: [UInt32: String] = [:]
    private var interpolationTask: Task<Void, Never>?
    private var transferTimeoutTask: Task<Void, Never>?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    var isConnected: Bool { connection == "Connected" && control != nil }

    func connect() {
        guard central.state == .poweredOn else {
            connection = "Bluetooth unavailable"
            return
        }
        connection = "Scanning…"
        central.scanForPeripherals(withServices: [CBUUID(string: BikeOSUUID.service)],
                                   options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
    }

    func disconnect() {
        if let peripheral { central.cancelPeripheralConnection(peripheral) }
    }

    func requestStatus() {
        sendControl(type: .statusRequest, payload: Data(), label: "Status requested")
    }

    func sendDemoRoute() {
        guard control != nil, data != nil, peripheral != nil else {
            lastAck = "Connect to BikeOS first"
            return
        }
        guard let mapURL = Bundle.main.url(forResource: "amsterdam_demo", withExtension: "bmap"),
              let routeURL = Bundle.main.url(forResource: "amsterdam_demo", withExtension: "brte"),
              let map = try? Data(contentsOf: mapURL),
              let route = try? Data(contentsOf: routeURL) else {
            lastAck = "Demo BMAP/BRTE assets are not in the app bundle"
            return
        }
        guard let decodedRoute = try? BikeOSRouteDecoder.decode(route) else {
            lastAck = "Bundled BRTE route failed validation"
            return
        }
        bundleID = UInt32(Date().timeIntervalSince1970) ^ 0xB1CE_0005
        routeID = decodedRoute.routeID
        transferID = bundleID
        transferBytes = map + route
        nextOffset = 0
        commitSent = false
        transferProgress = 0
        phase = .waitingForBeginAck

        var begin = Data()
        begin.appendLE(bundleID)
        begin.appendLE(routeID)
        begin.appendLE(UInt32(map.count))
        begin.appendLE(UInt32(route.count))
        begin.appendLE(BikeOSFrame.crc32(map))
        begin.appendLE(BikeOSFrame.crc32(route))
        begin.appendLE(BikeOSFrame.crc32(transferBytes))
        lastAck = "Preparing Amsterdam route (0%)"
        sendControl(type: .transferBegin, payload: begin, transferID: transferID, trackAck: false)
        armTransferTimeout()
    }

    func startNavigation() { sendDemoCommand(.startNavigation, label: "Navigation started") }
    func pauseNavigation() { sendDemoCommand(.pauseNavigation, label: "Navigation paused") }
    func resumeNavigation() { sendDemoCommand(.resumeNavigation, label: "Navigation resumed") }
    func restartNavigation() { sendDemoCommand(.restartNavigation, label: "Navigation restarted") }
    func stopNavigation() { sendDemoCommand(.stopNavigation, label: "Navigation stopped") }
    func triggerOffRoute() { sendDemoCommand(.triggerOffRoute, label: "Off-route test requested") }

    private func sendDemoCommand(_ type: BikeOSMessageType, label: String) {
        guard control != nil else {
            lastAck = "Connect to BikeOS first"
            return
        }
        sendControl(type: type, payload: Data(), label: label)
    }

    private func sendNextChunk() {
        guard phase == .sending, let peripheral, let data, nextOffset < transferBytes.count else { return }
        // Keep the complete frame within CoreBluetooth's negotiated write size
        // and the protocol's 480-byte payload limit. With-response writes give
        // transport-level flow control; the ESP32 ACK gives offset-level flow
        // control and validation.
        let maxFrame = min(500, peripheral.maximumWriteValueLength(for: .withResponse))
        let capacity = min(476, maxFrame - BikeOSFrame.headerSize - MemoryLayout<UInt32>.size)
        guard capacity > 0 else {
            phase = .failed
            lastAck = "Bluetooth write size is too small"
            return
        }
        let end = min(nextOffset + capacity, transferBytes.count)
        var chunk = Data()
        chunk.appendLE(UInt32(nextOffset))
        chunk.append(transferBytes[nextOffset..<end])
        let frame = BikeOSFrame(type: .transferChunk,
                                transferID: transferID,
                                sequence: nextSequence(),
                                flags: 0,
                                payload: chunk).encoded()
        guard !frame.isEmpty else {
            phase = .failed
            lastAck = "Could not encode route chunk"
            return
        }
        peripheral.writeValue(frame, for: data, type: .withResponse)
        lastAck = "Sending Amsterdam route (\(Int(Double(end) / Double(transferBytes.count) * 100))%)"
        armTransferTimeout()
    }

    private func sendCommit() {
        guard !commitSent else { return }
        commitSent = true
        phase = .verifying
        lastAck = "Verifying route on ESP32…"
        sendControl(type: .transferCommit, payload: Data(), transferID: transferID, trackAck: false)
        armTransferTimeout()
    }

    @discardableResult
    private func sendControl(type: BikeOSMessageType,
                             payload: Data,
                             transferID: UInt32 = 0,
                             label: String? = nil,
                             trackAck: Bool = true) -> UInt32 {
        guard let peripheral, let control else { return 0 }
        let frameSequence = nextSequence()
        let frame = BikeOSFrame(type: type,
                                transferID: transferID,
                                sequence: frameSequence,
                                flags: 0,
                                payload: payload).encoded()
        guard !frame.isEmpty else {
            lastAck = "Could not encode command"
            return 0
        }
        if trackAck, let label { pendingCommands[frameSequence] = label }
        peripheral.writeValue(frame, for: control, type: .withResponse)
        return frameSequence
    }

    private func armTransferTimeout() {
        transferTimeoutTask?.cancel()
        transferTimeoutTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 15_000_000_000)
            guard !Task.isCancelled, let self else { return }
            guard self.phase == .waitingForBeginAck || self.phase == .sending || self.phase == .verifying else { return }
            self.phase = .failed
            self.lastAck = "Route transfer timed out — reconnect and try again"
        }
    }

    private func clearTransferTimeout() {
        transferTimeoutTask?.cancel()
        transferTimeoutTask = nil
    }

    private func nextSequence() -> UInt32 {
        defer { sequence &+= 1 }
        return sequence
    }

    private func handleAcknowledgement(_ frame: BikeOSFrame) {
        guard let acknowledgement = BikeOSAcknowledgement.decode(frame) else { return }

        if frame.transferID == 0, pendingCommands.removeValue(forKey: frame.sequence) != nil {
            if acknowledgement.result == .ok || acknowledgement.result == .accepted {
                lastAck = acknowledgement.result == .accepted ? "Command accepted" : "Command completed"
            } else {
                lastAck = "ESP32 rejected command: \(acknowledgement.result.label)"
            }
            return
        }

        guard frame.transferID == transferID else { return }
        switch phase {
        case .waitingForBeginAck where acknowledgement.result == .accepted:
            phase = .sending
            nextOffset = 0
            sendNextChunk()
        case .sending where acknowledgement.result == .accepted:
            guard acknowledgement.bundleID == bundleID,
                  acknowledgement.nextOffset >= UInt32(nextOffset),
                  acknowledgement.nextOffset <= UInt32(transferBytes.count) else {
                phase = .failed
                lastAck = "ESP32 rejected chunk offset"
                clearTransferTimeout()
                return
            }
            nextOffset = Int(acknowledgement.nextOffset)
            transferProgress = Double(nextOffset) / Double(max(1, transferBytes.count))
            if nextOffset == transferBytes.count { sendCommit() } else { sendNextChunk() }
        case .verifying where acknowledgement.result == .ok:
            lastAck = "Route committed; verifying active route…"
            requestStatus()
            armTransferTimeout()
        case .waitingForBeginAck, .sending, .verifying:
            phase = .failed
            lastAck = "ESP32 transfer error: \(acknowledgement.result.label)"
            clearTransferTimeout()
        default:
            break
        }
    }

    private func ingestStatus(_ newStatus: BikeOSStatus) {
        status = newStatus
        liveHeading = newStatus.headingDeg
        guard newStatus.gpsValid else { return }

        let target = newStatus.coordinate
        let start = liveCoordinate ?? target
        interpolationTask?.cancel()
        interpolationTask = Task { [weak self] in
            for step in 1...10 {
                if Task.isCancelled { return }
                try? await Task.sleep(nanoseconds: 50_000_000)
                if Task.isCancelled { return }
                let t = Double(step) / 10.0
                self?.liveCoordinate = CLLocationCoordinate2D(
                    latitude: start.latitude + (target.latitude - start.latitude) * t,
                    longitude: start.longitude + (target.longitude - start.longitude) * t)
            }
        }
    }
}

extension BikeOSBluetooth: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        Task { @MainActor in
            self.connection = central.state == .poweredOn ? "Ready — tap Connect" : "Bluetooth unavailable"
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                   didDiscover peripheral: CBPeripheral,
                                   advertisementData: [String: Any],
                                   rssi RSSI: NSNumber) {
        Task { @MainActor in
            self.peripheral = peripheral
            self.deviceName = peripheral.name ?? "BikeOS"
            self.connection = "Connecting…"
            central.stopScan()
            peripheral.delegate = self
            central.connect(peripheral)
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        Task { @MainActor in
            self.connection = "Connected"
            peripheral.discoverServices([CBUUID(string: BikeOSUUID.service)])
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                   didDisconnectPeripheral peripheral: CBPeripheral,
                                   error: Error?) {
        Task { @MainActor in
            self.connection = "Disconnected"
            self.status = nil
            self.liveCoordinate = nil
            self.liveHeading = nil
            self.pendingCommands.removeAll()
            self.phase = .idle
            self.clearTransferTimeout()
        }
    }
}

extension BikeOSBluetooth: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let service = peripheral.services?.first(where: { $0.uuid == CBUUID(string: BikeOSUUID.service) }) else { return }
        peripheral.discoverCharacteristics(nil, for: service)
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                didDiscoverCharacteristicsFor service: CBService,
                                error: Error?) {
        Task { @MainActor in
            for characteristic in service.characteristics ?? [] {
                switch characteristic.uuid.uuidString.uppercased() {
                case BikeOSUUID.control: control = characteristic
                case BikeOSUUID.data: data = characteristic
                case BikeOSUUID.acknowledgement: peripheral.setNotifyValue(true, for: characteristic)
                case BikeOSUUID.status: peripheral.setNotifyValue(true, for: characteristic)
                default: break
                }
            }
            requestStatus()
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                didUpdateValueFor characteristic: CBCharacteristic,
                                error: Error?) {
        guard error == nil, let value = characteristic.value, let frame = BikeOSFrame.decode(value) else { return }
        Task { @MainActor in
            if frame.type == .acknowledgement {
                handleAcknowledgement(frame)
            } else if let decoded = BikeOSStatus.decode(frame) {
                ingestStatus(decoded)
                if phase == .verifying, decoded.routeID == routeID {
                    phase = .activated
                    clearTransferTimeout()
                    transferProgress = 1
                    lastAck = "Activated route \(routeID) on ESP32"
                }
            }
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                didWriteValueFor characteristic: CBCharacteristic,
                                error: Error?) {
        if let error {
            Task { @MainActor in
                phase = .failed
                lastAck = "Bluetooth write failed: \(error.localizedDescription)"
            }
        }
    }
}
