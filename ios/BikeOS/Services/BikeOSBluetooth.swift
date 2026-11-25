import CoreBluetooth
import CoreLocation
import Combine
import Foundation

enum BikeOSConnectionState: Equatable {
    case bluetoothOff
    case readyToScan
    case scanning
    case connecting
    case connected
    case readyForTransfer
    case reconnecting
    case disconnected
    case failed

    var label: String {
        switch self {
        case .bluetoothOff: "Bluetooth unavailable"
        case .readyToScan: "Ready — tap Connect"
        case .scanning: "Scanning…"
        case .connecting: "Connecting…"
        case .connected: "Connected"
        case .readyForTransfer: "Ready"
        case .reconnecting: "Reconnecting…"
        case .disconnected: "Disconnected"
        case .failed: "Connection failed"
        }
    }
}

struct BikeOSWritePlan {
    let mode: CBCharacteristicWriteType
    let limit: Int
    let responseLimit: Int
    let noResponseLimit: Int
    let fragmented: Bool
}

enum BikeOSBLEFragment {
    static let magic: UInt8 = 0xF7
    static let version: UInt8 = 1
    static let headerSize = 8

    static func makeFragments(frame: Data, messageID: UInt16, maxWriteLength: Int) -> [Data]? {
        guard frame.count >= BikeOSFrame.headerSize, frame.count <= UInt16.max,
              maxWriteLength > headerSize else { return nil }
        let payloadCapacity = maxWriteLength - headerSize
        var fragments: [Data] = []
        var offset = 0
        while offset < frame.count {
            let end = min(offset + payloadCapacity, frame.count)
            var fragment = Data([magic, version])
            fragment.appendLE(messageID)
            fragment.appendLE(UInt16(frame.count))
            fragment.appendLE(UInt16(offset))
            fragment.append(frame[offset..<end])
            fragments.append(fragment)
            offset = end
        }
        return fragments
    }
}

private struct BikeOSRideCheckpoint: Codable {
    let routeID: UInt32
    let routeData: Data
    let routeName: String
    let routeSource: String
    var progressM: Double
    var latitude: Double
    var longitude: Double
    var headingDeg: Double?
    var etaMinutesLocal: UInt16?
    var runStateRaw: UInt8
    var updatedAt: Date

    var coordinate: CLLocationCoordinate2D {
        CLLocationCoordinate2D(latitude: latitude, longitude: longitude)
    }

    var restoredRunState: BikeOSRunState {
        BikeOSRunState(rawValue: runStateRaw) ?? .paused
    }
}

@MainActor final class BikeOSBluetooth: NSObject, ObservableObject {
    @Published private(set) var connectionState: BikeOSConnectionState = .disconnected
    @Published private(set) var status: BikeOSStatus?
    @Published private(set) var lastAck = "No transfer yet"
    @Published private(set) var deviceName = "BikeOS"
    @Published private(set) var firmwareInfo = ""
    @Published private(set) var transferProgress: Double?
    @Published private(set) var liveCoordinate: CLLocationCoordinate2D?
    @Published private(set) var liveHeading: Double?
    @Published private(set) var pendingNavigationCommand: BikeOSMessageType?
    @Published private(set) var activeRouteID: UInt32?
    @Published private(set) var transferringRouteID: UInt32?
    @Published private(set) var restoredRoute: BikeOSRoutePlan?

    private enum TransferPhase { case idle, waitingForBeginAck, sending, verifying, activated, failed }
    private var phase: TransferPhase = .idle
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var preferredPeripheralID: UUID?
    private var connectionRequested = false
    private var reconnectAttempt = 0
    private var reconnectTask: Task<Void, Never>?
    private var scanTimeoutTask: Task<Void, Never>?
    private var control: CBCharacteristic?
    private var data: CBCharacteristic?
    private var sequence: UInt32 = 1
    private var transferID: UInt32 = 0
    private var bundleID: UInt32 = 0
    private var routeID: UInt32 = 0
    private var transferBytes = Data()
    private var nextOffset = 0
    private var commitSent = false
    private var pendingCommands: [UInt32: (label: String, type: BikeOSMessageType)] = [:]
    private var interpolationTask: Task<Void, Never>?
    private var transferTimeoutTask: Task<Void, Never>?
    private var commandTimeoutTask: Task<Void, Never>?
    private var ackNotifyReady = false
    private var statusNotifyReady = false
    private var fragmentMessageID: UInt16 = 1
    private struct PendingWrite {
        let bytes: Data
        let characteristic: CBCharacteristic
        let peripheral: CBPeripheral
        let type: CBCharacteristicWriteType
    }
    private var writeQueue: [PendingWrite] = []
    private var responseWriteInFlight = false
    private let checkpointKey = "BikeOS.activeRideCheckpoint"
    private var checkpoint: BikeOSRideCheckpoint?
    private var activeRoutePlan: BikeOSRoutePlan?
    private var pendingRestore: BikeOSRideCheckpoint?
    private var restoreAttemptedForConnection = false
    private var lastCheckpointWrite = Date.distantPast

    override init() {
        super.init()
        if let data = UserDefaults.standard.data(forKey: checkpointKey),
           let saved = try? JSONDecoder().decode(BikeOSRideCheckpoint.self, from: data),
           let route = try? BikeOSRouteDecoder.decode(saved.routeData,
                                                     name: saved.routeName,
                                                     source: saved.routeSource) {
            checkpoint = saved
            activeRoutePlan = route
            restoredRoute = route
            liveCoordinate = saved.coordinate
            liveHeading = saved.headingDeg
        }
        central = CBCentralManager(delegate: self, queue: nil,
                                   options: [CBCentralManagerOptionRestoreIdentifierKey: "BikeOS.Bluetooth"])
    }

    var connection: String { connectionState.label }
    var isBluetoothConnected: Bool { peripheral?.state == .connected }
    var isReadyForTransfer: Bool { connectionState == .readyForTransfer }
    var isConnected: Bool { isReadyForTransfer }
    var isTransferringRoute: Bool { transferringRouteID != nil }

    func routeIsActive(_ route: BikeOSRoutePlan) -> Bool {
        activeRouteID == route.routeID || status?.routeID == route.routeID
    }

    var hasSavedRide: Bool { checkpoint != nil }

    func connect() {
        connectionRequested = true
        reconnectAttempt = 0
        reconnectTask?.cancel()
        guard central.state == .poweredOn else {
            connectionState = .bluetoothOff
            return
        }
        beginScanOrRetrieve()
    }

    func disconnect() {
        connectionRequested = false
        reconnectTask?.cancel()
        scanTimeoutTask?.cancel()
        central.stopScan()
        if let peripheral { central.cancelPeripheralConnection(peripheral) }
        connectionState = .disconnected
    }

    private func beginScanOrRetrieve() {
        guard connectionRequested, central.state == .poweredOn else { return }
        if let id = preferredPeripheralID,
           let known = central.retrievePeripherals(withIdentifiers: [id]).first {
            peripheral = known
            known.delegate = self
            connectionState = .connecting
            central.connect(known)
            return
        }
        connectionState = reconnectAttempt == 0 ? .scanning : .reconnecting
        central.scanForPeripherals(withServices: [CBUUID(string: BikeOSUUID.service)],
                                   options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
        scanTimeoutTask?.cancel()
        scanTimeoutTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 10_000_000_000)
            guard !Task.isCancelled, let self, self.connectionRequested else { return }
            self.central.stopScan()
            self.scheduleReconnect(reason: "ESP32 was not found")
        }
    }

    private func scheduleReconnect(reason: String) {
        guard connectionRequested, central.state == .poweredOn else { return }
        reconnectAttempt += 1
        let delay = min(30, 1 << min(reconnectAttempt - 1, 5))
        connectionState = .reconnecting
        lastAck = "(reason). Reconnecting in (delay)s…"
        reconnectTask?.cancel()
        reconnectTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: UInt64(delay) * 1_000_000_000)
            guard !Task.isCancelled, let self, self.connectionRequested else { return }
            self.beginScanOrRetrieve()
        }
    }

    func requestStatus() {
        sendControl(type: .statusRequest, payload: Data(), label: "Status requested")
    }

    func sendDemoRoute() {
        guard isReadyForTransfer else {
            lastAck = "Connect to BikeOS and wait until it is Ready"
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
        beginTransfer(route: decodedRoute, map: map, routeBytes: route, displayName: "Amsterdam route")
    }

    func sendRoute(_ route: BikeOSRoutePlan) {
        guard isReadyForTransfer else {
            lastAck = "Connect to BikeOS and wait until it is Ready"
            return
        }
        guard !isTransferringRoute else {
            lastAck = "A route is already being transferred — wait for activation"
            return
        }
        do {
            let routeBytes = try BikeOSRouteEncoder.encode(route)
            beginTransfer(route: route, map: Data(), routeBytes: routeBytes, displayName: route.name)
        } catch {
            lastAck = error.localizedDescription
        }
    }

    private func beginTransfer(route decodedRoute: BikeOSRoutePlan,
                               map: Data,
                               routeBytes route: Data,
                               displayName: String,
                               preserveCheckpoint: Bool = false) {
        activeRoutePlan = decodedRoute
        restoredRoute = decodedRoute
        saveRouteCheckpoint(decodedRoute, routeBytes: route, preserveProgress: preserveCheckpoint)
        bundleID = UInt32(Date().timeIntervalSince1970) ^ 0xB1CE_0005
        routeID = decodedRoute.routeID
        activeRouteID = nil
        transferringRouteID = routeID
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
        lastAck = "Preparing \(displayName) (0%)"
        guard sendControl(type: .transferBegin, payload: begin, transferID: transferID, trackAck: false) != 0 else {
            phase = .failed
            transferringRouteID = nil
            lastAck = "Could not start route transfer — reconnect and try again"
            return
        }
        armTransferTimeout()
    }

    func startNavigation() { sendDemoCommand(.startNavigation, label: "Starting navigation") }
    func pauseNavigation() { sendDemoCommand(.pauseNavigation, label: "Pausing navigation") }
    func resumeNavigation() { sendDemoCommand(.resumeNavigation, label: "Resuming navigation") }
    func restartNavigation() { sendDemoCommand(.restartNavigation, label: "Restarting navigation") }
    func stopNavigation() { sendDemoCommand(.stopNavigation, label: "Stopping navigation") }
    func triggerOffRoute() { sendDemoCommand(.triggerOffRoute, label: "Off-route test requested") }

    private func sendRestoreNavigation(_ saved: BikeOSRideCheckpoint) {
        guard isReadyForTransfer, let activeRoutePlan,
              activeRoutePlan.routeID == saved.routeID else { return }
        sendRouteETA(routeID: saved.routeID,
                     etaMinutesLocal: etaMinutesLocal(for: activeRoutePlan, progressM: saved.progressM))
        let progressCm = UInt32(max(0, min(Double(UInt32.max) / 100.0, saved.progressM)) * 100.0)
        var payload = Data()
        payload.appendLE(saved.routeID)
        payload.appendLE(progressCm)
        // A reboot always restores paused. This prevents the simulator or a
        // later real-GPS session from moving while the rider is not ready.
        payload.append(BikeOSRunState.paused.rawValue)
        pendingNavigationCommand = .restoreNavigation
        let sequence = sendControl(type: .restoreNavigation,
                                   payload: payload,
                                   label: "Restoring saved ride")
        if sequence != 0 {
            lastAck = "Restoring your saved position…"
            commandTimeoutTask?.cancel()
            commandTimeoutTask = Task { [weak self] in
                try? await Task.sleep(nanoseconds: 5_000_000_000)
                guard !Task.isCancelled, let self,
                      self.pendingNavigationCommand == .restoreNavigation else { return }
                self.pendingNavigationCommand = nil
                self.pendingCommands.removeValue(forKey: sequence)
                self.lastAck = "ESP32 did not confirm the saved position — try reconnecting"
            }
        } else {
            pendingNavigationCommand = nil
        }
    }

    private func saveRouteCheckpoint(_ route: BikeOSRoutePlan, routeBytes: Data, preserveProgress: Bool) {
        let coordinate = preserveProgress && checkpoint?.routeID == route.routeID
            ? checkpoint!.coordinate
            : (route.points.first ?? CLLocationCoordinate2D())
        let saved = BikeOSRideCheckpoint(routeID: route.routeID,
                                         routeData: routeBytes,
                                         routeName: route.name,
                                         routeSource: route.source,
                                         progressM: preserveProgress && checkpoint?.routeID == route.routeID ? checkpoint!.progressM : 0,
                                         latitude: coordinate.latitude,
                                         longitude: coordinate.longitude,
                                         headingDeg: preserveProgress && checkpoint?.routeID == route.routeID ? checkpoint!.headingDeg : nil,
                                         etaMinutesLocal: preserveProgress && checkpoint?.routeID == route.routeID
                                             ? checkpoint!.etaMinutesLocal
                                             : etaMinutesLocal(for: route),
                                         runStateRaw: BikeOSRunState.notStarted.rawValue,
                                         updatedAt: Date())
        checkpoint = saved
        persistCheckpoint(saved)
    }

    private func etaMinutesLocal(for route: BikeOSRoutePlan, progressM: Double = 0) -> UInt16 {
        let completed = max(0, min(route.distanceM, progressM))
        let remainingFraction = route.distanceM > 1 ? max(0, 1 - completed / route.distanceM) : 0
        let remainingSeconds = Double(max(0, route.durationMin)) * 60.0 * remainingFraction
        let arrival = Date().addingTimeInterval(remainingSeconds)
        let components = Calendar.current.dateComponents([.hour, .minute], from: arrival)
        let minutes = (components.hour ?? 0) * 60 + (components.minute ?? 0)
        return UInt16(max(0, min(1439, minutes)))
    }

    private func sendRouteETA(routeID: UInt32, etaMinutesLocal: UInt16) {
        guard isReadyForTransfer, etaMinutesLocal < 1440 else { return }
        var payload = Data()
        payload.appendLE(routeID)
        payload.appendLE(etaMinutesLocal)
        _ = sendControl(type: .setRouteETA, payload: payload, label: "Sending arrival time")
    }

    private func persistCheckpoint(_ saved: BikeOSRideCheckpoint) {
        guard let data = try? JSONEncoder().encode(saved) else { return }
        UserDefaults.standard.set(data, forKey: checkpointKey)
    }

    private func maybeRestore(_ newStatus: BikeOSStatus) {
        guard !restoreAttemptedForConnection, pendingNavigationCommand == nil,
              let saved = checkpoint, saved.progressM > 5 else { return }
        restoreAttemptedForConnection = true
        if newStatus.routeID != saved.routeID {
            guard let route = try? BikeOSRouteDecoder.decode(saved.routeData,
                                                             name: saved.routeName,
                                                             source: saved.routeSource) else {
                lastAck = "Saved ride could not be decoded — send the route again"
                return
            }
            activeRoutePlan = route
            restoredRoute = route
            pendingRestore = saved
            beginTransfer(route: route,
                          map: Data(),
                          routeBytes: saved.routeData,
                          displayName: "saved ride",
                          preserveCheckpoint: true)
        } else if newStatus.runState == .notStarted || newStatus.runState == .stopped {
            activeRoutePlan = activeRoutePlan ?? restoredRoute
            sendRestoreNavigation(saved)
        }
    }

    private func updateCheckpoint(for newStatus: BikeOSStatus) {
        guard var saved = checkpoint, saved.routeID == newStatus.routeID,
              let route = activeRoutePlan ?? restoredRoute else { return }
        let progress = max(0, min(route.distanceM, route.distanceM - Double(newStatus.remainingM)))
        saved.progressM = progress
        if newStatus.gpsValid {
            saved.latitude = newStatus.coordinate.latitude
            saved.longitude = newStatus.coordinate.longitude
        }
        saved.headingDeg = newStatus.headingDeg
        saved.runStateRaw = newStatus.runState.rawValue
        saved.updatedAt = Date()
        checkpoint = saved
        if Date().timeIntervalSince(lastCheckpointWrite) >= 0.5 || newStatus.runState != .running {
            lastCheckpointWrite = Date()
            persistCheckpoint(saved)
        }
    }

    private func sendDemoCommand(_ type: BikeOSMessageType, label: String) {
        guard isReadyForTransfer else {
            lastAck = "Connect to BikeOS and wait until it is Ready"
            return
        }
        guard pendingNavigationCommand == nil else {
            lastAck = "Waiting for ESP32 to confirm the previous command"
            return
        }
        pendingNavigationCommand = type
        let sequence = sendControl(type: type, payload: Data(), label: label)
        if sequence == 0 {
            pendingNavigationCommand = nil
            return
        }
        commandTimeoutTask?.cancel()
        commandTimeoutTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 5_000_000_000)
            guard !Task.isCancelled, let self else { return }
            guard self.pendingNavigationCommand == type else { return }
            self.pendingNavigationCommand = nil
            self.pendingCommands.removeValue(forKey: sequence)
            self.lastAck = "ESP32 did not confirm the command — try again"
        }
    }

    private func sendNextChunk() {
        guard phase == .sending, let peripheral, let data, nextOffset < transferBytes.count else { return }
        // Keep the protocol frame at its full 480-byte payload capacity. The
        // write layer fragments it when the negotiated ATT limit is smaller.
        let capacity = 476
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
            transferringRouteID = nil
            lastAck = "Could not encode route chunk"
            return
        }
        guard write(frame, to: data, on: peripheral) else { return }
        lastAck = "Sending route (\(Int(Double(end) / Double(max(1, transferBytes.count)) * 100))%)"
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
        if trackAck, let label { pendingCommands[frameSequence] = (label: label, type: type) }
        guard write(frame, to: control, on: peripheral) else {
            pendingCommands.removeValue(forKey: frameSequence)
            return 0
        }
        return frameSequence
    }

    private func write(_ frame: Data, to characteristic: CBCharacteristic, on peripheral: CBPeripheral) -> Bool {
        let responseLimit = peripheral.maximumWriteValueLength(for: .withResponse)
        if frame.count <= responseLimit, characteristic.properties.contains(.write) {
            enqueue([PendingWrite(bytes: frame, characteristic: characteristic, peripheral: peripheral, type: .withResponse)])
            return true
        }
        let noResponseLimit = peripheral.maximumWriteValueLength(for: .withoutResponse)
        if frame.count <= noResponseLimit, characteristic.properties.contains(.writeWithoutResponse) {
            enqueue([PendingWrite(bytes: frame, characteristic: characteristic, peripheral: peripheral, type: .withoutResponse)])
            return true
        }

        let mode: CBCharacteristicWriteType
        let limit: Int
        if characteristic.properties.contains(.write), responseLimit > BikeOSBLEFragment.headerSize {
            mode = .withResponse
            limit = responseLimit
        } else if characteristic.properties.contains(.writeWithoutResponse), noResponseLimit > BikeOSBLEFragment.headerSize {
            mode = .withoutResponse
            limit = noResponseLimit
        } else {
            failWrite(frame: frame,
                      responseLimit: responseLimit,
                      noResponseLimit: noResponseLimit,
                      properties: characteristic.properties)
            return false
        }
        guard let fragments = BikeOSBLEFragment.makeFragments(frame: frame,
                                                               messageID: nextFragmentMessageID(),
                                                               maxWriteLength: limit) else {
            failWrite(frame: frame,
                      responseLimit: responseLimit,
                      noResponseLimit: noResponseLimit,
                      properties: characteristic.properties)
            return false
        }
        enqueue(fragments.map { PendingWrite(bytes: $0, characteristic: characteristic, peripheral: peripheral, type: mode) })
        return true
    }

    private func nextFragmentMessageID() -> UInt16 {
        defer { fragmentMessageID &+= 1 }
        return fragmentMessageID
    }

    private func enqueue(_ writes: [PendingWrite]) {
        writeQueue.append(contentsOf: writes)
        pumpWrites()
    }

    private func pumpWrites() {
        while !writeQueue.isEmpty {
            let next = writeQueue[0]
            if next.type == .withResponse {
                guard !responseWriteInFlight else { return }
                responseWriteInFlight = true
                writeQueue.removeFirst()
                next.peripheral.writeValue(next.bytes, for: next.characteristic, type: next.type)
                return
            }
            guard next.peripheral.canSendWriteWithoutResponse else { return }
            writeQueue.removeFirst()
            next.peripheral.writeValue(next.bytes, for: next.characteristic, type: next.type)
        }
    }

    private func failWrite(frame: Data,
                           responseLimit: Int,
                           noResponseLimit: Int,
                           properties: CBCharacteristicProperties) {
        writeQueue.removeAll()
        responseWriteInFlight = false
        phase = .failed
        transferringRouteID = nil
        lastAck = "BLE frame \(frame.count) B exceeds write limits (response \(responseLimit), no-response \(noResponseLimit)); properties=\(propertiesDescription(properties)); firmware=\(firmwareInfo.isEmpty ? "unknown" : firmwareInfo)"
        clearTransferTimeout()
    }

    private func propertiesDescription(_ properties: CBCharacteristicProperties) -> String {
        var names: [String] = []
        if properties.contains(.write) { names.append("write") }
        if properties.contains(.writeWithoutResponse) { names.append("writeWithoutResponse") }
        if properties.contains(.notify) { names.append("notify") }
        return names.isEmpty ? "none" : names.joined(separator: ",")
    }

    private func armTransferTimeout() {
        transferTimeoutTask?.cancel()
        transferTimeoutTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 15_000_000_000)
            guard !Task.isCancelled, let self else { return }
            guard self.phase == .waitingForBeginAck || self.phase == .sending || self.phase == .verifying else { return }
            self.phase = .failed
            self.transferringRouteID = nil
            self.lastAck = "Route transfer timed out — reconnect and try again"
        }
    }

    private func clearTransferTimeout() {
        transferTimeoutTask?.cancel()
        transferTimeoutTask = nil
    }

    private func updateTransferReadiness() {
        guard isBluetoothConnected, control != nil, data != nil else {
            if isBluetoothConnected { connectionState = .connected }
            return
        }
        guard ackNotifyReady, statusNotifyReady else {
            connectionState = .connected
            return
        }
        connectionState = .readyForTransfer
        requestStatus()
    }

    private func nextSequence() -> UInt32 {
        defer { sequence &+= 1 }
        return sequence
    }

    private func handleAcknowledgement(_ frame: BikeOSFrame) {
        guard let acknowledgement = BikeOSAcknowledgement.decode(frame) else { return }

        if frame.transferID == 0, let command = pendingCommands.removeValue(forKey: frame.sequence) {
            if pendingNavigationCommand == command.type {
                pendingNavigationCommand = nil
                commandTimeoutTask?.cancel()
                commandTimeoutTask = nil
            }
            if acknowledgement.result == .ok || acknowledgement.result == .accepted {
                lastAck = acknowledgement.result == .accepted ? "\(command.label) accepted" : "\(command.label) completed"
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
                transferringRouteID = nil
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
            transferringRouteID = nil
            lastAck = "ESP32 transfer error: \(acknowledgement.result.label)"
            clearTransferTimeout()
        default:
            break
        }
    }

    private func ingestStatus(_ newStatus: BikeOSStatus) {
        status = newStatus
        liveHeading = newStatus.headingDeg
        maybeRestore(newStatus)
        updateCheckpoint(for: newStatus)
        guard newStatus.gpsValid else { return }

        let target = newStatus.coordinate
        if newStatus.runState != .running {
            interpolationTask?.cancel()
            // A reboot, pause, or restore is an authoritative position update.
            // Never keep showing the pre-reboot marker while the ESP32 reports
            // its current checkpoint.
            liveCoordinate = target
            return
        }
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
            if central.state != .poweredOn {
                self.connectionState = .bluetoothOff
                self.reconnectTask?.cancel()
                self.scanTimeoutTask?.cancel()
                self.central.stopScan()
            } else if self.connectionRequested {
                self.beginScanOrRetrieve()
            } else {
                self.connectionState = .readyToScan
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, willRestoreState dictionary: [String : Any]) {
        Task { @MainActor in
            if let restored = dictionary[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral],
               let restoredPeripheral = restored.first {
                self.peripheral = restoredPeripheral
                self.preferredPeripheralID = restoredPeripheral.identifier
                self.connectionRequested = true
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                   didDiscover peripheral: CBPeripheral,
                                   advertisementData: [String: Any],
                                   rssi RSSI: NSNumber) {
        Task { @MainActor in
            self.peripheral = peripheral
            self.preferredPeripheralID = peripheral.identifier
            self.connectionRequested = true
            self.reconnectAttempt = 0
            self.scanTimeoutTask?.cancel()
            self.deviceName = peripheral.name ?? "BikeOS"
            self.connectionState = .connecting
            central.stopScan()
            peripheral.delegate = self
            central.connect(peripheral)
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        Task { @MainActor in
            self.control = nil
            self.data = nil
            self.ackNotifyReady = false
            self.statusNotifyReady = false
            self.restoreAttemptedForConnection = false
            self.reconnectAttempt = 0
            self.reconnectTask?.cancel()
            self.connectionState = .connected
            peripheral.discoverServices([CBUUID(string: BikeOSUUID.service)])
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                   didFailToConnect peripheral: CBPeripheral,
                                   error: Error?) {
        Task { @MainActor in
            self.connectionState = .failed
            self.scheduleReconnect(reason: error?.localizedDescription ?? "Connection failed")
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                   didDisconnectPeripheral peripheral: CBPeripheral,
                                   error: Error?) {
        Task { @MainActor in
            let shouldReconnect = self.connectionRequested
            self.connectionState = shouldReconnect ? .reconnecting : .disconnected
            self.transferringRouteID = nil
            self.control = nil
            self.data = nil
            self.ackNotifyReady = false
            self.statusNotifyReady = false
            self.pendingCommands.removeAll()
            self.pendingNavigationCommand = nil
            self.commandTimeoutTask?.cancel()
            self.commandTimeoutTask = nil
            self.writeQueue.removeAll()
            self.responseWriteInFlight = false
            self.phase = .idle
            self.clearTransferTimeout()
            self.interpolationTask?.cancel()
            if shouldReconnect { self.scheduleReconnect(reason: "ESP32 disconnected") }
        }
    }
}

extension BikeOSBluetooth: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if error != nil {
            Task { @MainActor in self.connectionState = .failed }
            return
        }
        guard let service = peripheral.services?.first(where: { $0.uuid == CBUUID(string: BikeOSUUID.service) }) else { return }
        peripheral.discoverCharacteristics(nil, for: service)
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                didDiscoverCharacteristicsFor service: CBService,
                                error: Error?) {
        if error != nil {
            Task { @MainActor in self.connectionState = .failed }
            return
        }
        Task { @MainActor in
            for characteristic in service.characteristics ?? [] {
                switch characteristic.uuid.uuidString.uppercased() {
                case BikeOSUUID.deviceInfo:
                    peripheral.readValue(for: characteristic)
                case BikeOSUUID.control:
                    control = characteristic
                case BikeOSUUID.data:
                    data = characteristic
                case BikeOSUUID.acknowledgement:
                    peripheral.setNotifyValue(true, for: characteristic)
                case BikeOSUUID.status:
                    peripheral.setNotifyValue(true, for: characteristic)
                default: break
                }
            }
            updateTransferReadiness()
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                didUpdateNotificationStateFor characteristic: CBCharacteristic,
                                error: Error?) {
        Task { @MainActor in
            if error != nil {
                self.connectionState = .failed
                self.lastAck = "Bluetooth notification setup failed"
                return
            }
            switch characteristic.uuid.uuidString.uppercased() {
            case BikeOSUUID.acknowledgement: self.ackNotifyReady = characteristic.isNotifying
            case BikeOSUUID.status: self.statusNotifyReady = characteristic.isNotifying
            default: break
            }
            self.updateTransferReadiness()
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                didUpdateValueFor characteristic: CBCharacteristic,
                                error: Error?) {
        guard error == nil, let value = characteristic.value else { return }
        Task { @MainActor in
            if characteristic.uuid.uuidString.uppercased() == BikeOSUUID.deviceInfo {
                self.firmwareInfo = String(data: value, encoding: .utf8) ?? "Unknown firmware"
                return
            }
            guard let frame = BikeOSFrame.decode(value) else { return }
            if frame.type == .acknowledgement {
                handleAcknowledgement(frame)
            } else if let decoded = BikeOSStatus.decode(frame) {
                ingestStatus(decoded)
                if phase == .verifying, decoded.routeID == routeID {
                    phase = .activated
                    clearTransferTimeout()
                    transferProgress = 1
                    activeRouteID = routeID
                    transferringRouteID = nil
                    lastAck = "Activated route \(routeID) on ESP32"
                    if let saved = pendingRestore {
                        pendingRestore = nil
                        sendRestoreNavigation(saved)
                    } else if let activeRoutePlan {
                        sendRouteETA(routeID: routeID, etaMinutesLocal: etaMinutesLocal(for: activeRoutePlan))
                    }
                }
            }
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                didWriteValueFor characteristic: CBCharacteristic,
                                error: Error?) {
        Task { @MainActor in
            self.responseWriteInFlight = false
            if let error {
                phase = .failed
                transferringRouteID = nil
                lastAck = "Bluetooth write failed: \(error.localizedDescription)"
                self.writeQueue.removeAll()
                self.clearTransferTimeout()
                return
            }
            self.pumpWrites()
        }
    }

    nonisolated func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
        Task { @MainActor in self.pumpWrites() }
    }
}
