import Foundation

enum BikeOSMessageType: UInt8 {
    case statusRequest = 1, transferBegin = 2, transferChunk = 3, transferCommit = 4
    case transferCancel = 5, startNavigation = 6, cancelNavigation = 7
    case acknowledgement = 8, error = 9, status = 10, rerouteRequest = 11
}

enum BikeOSResultCode: UInt8 { case ok = 0, accepted = 1, invalidFrame = 2, unsupportedVersion = 3, busy = 4, invalidTransfer = 5, invalidOffset = 6, crcMismatch = 7, invalidBundle = 8, cancelled = 9, unauthorized = 10 }
enum BikeOSNavigationState: UInt8 { case noRoute = 0, waitingForGPS = 1, navigating = 2, gpsLost = 3, offRoute = 4, rerouting = 5, phoneDisconnected = 6, destinationReached = 7, error = 8 }
enum BikeOSGpsSource: UInt8 { case none = 0, simulated = 1, uart = 2, iPhoneFallback = 3 }

struct BikeOSFrame {
    static let version: UInt8 = 1
    static let headerSize = 20
    let type: BikeOSMessageType
    let transferID: UInt32
    let sequence: UInt32
    let flags: UInt16
    let payload: Data

    func encoded() -> Data {
        guard payload.count <= 480 else { return Data() }
        var out = Data([0x42, 0x4B, Self.version, type.rawValue])
        out.appendLE(transferID); out.appendLE(sequence); out.appendLE(UInt16(payload.count)); out.appendLE(flags); out.appendLE(Self.crc32(payload)); out.append(payload)
        return out
    }

    static func decode(_ data: Data) -> BikeOSFrame? {
        guard data.count >= headerSize, data[0] == 0x42, data[1] == 0x4B, data[2] == version,
              let type = BikeOSMessageType(rawValue: data[3]) else { return nil }
        let transferID = data.leUInt32(at: 4), sequence = data.leUInt32(at: 8), length = Int(data.leUInt16(at: 12))
        guard length <= 480, data.count == headerSize + length, data.leUInt32(at: 16) == crc32(data.subdata(in: headerSize..<data.count)) else { return nil }
        return BikeOSFrame(type: type, transferID: transferID, sequence: sequence, flags: data.leUInt16(at: 14), payload: data.subdata(in: headerSize..<data.count))
    }

    static func crc32(_ data: Data) -> UInt32 {
        var crc: UInt32 = 0xFFFF_FFFF
        for byte in data { crc ^= UInt32(byte); for _ in 0..<8 { crc = (crc >> 1) ^ (0xEDB88320 & (0 &- (crc & 1))) } }
        return ~crc
    }
}

struct BikeOSStatus {
    let state: BikeOSNavigationState
    let gps: BikeOSGpsSource
    let flags: UInt8
    let latitudeE7: Int32
    let longitudeE7: Int32
    let routeID: UInt32
    let remainingM: UInt32
    let nextManeuverM: UInt32
    let freeHeap: UInt32

    static func decode(_ frame: BikeOSFrame) -> BikeOSStatus? {
        guard frame.type == .status || frame.type == .rerouteRequest, frame.payload.count == 28, frame.payload[0] == 1,
              let state = BikeOSNavigationState(rawValue: frame.payload[1]), let gps = BikeOSGpsSource(rawValue: frame.payload[2]) else { return nil }
        return BikeOSStatus(state: state, gps: gps, flags: frame.payload[3], latitudeE7: Int32(bitPattern: frame.payload.leUInt32(at: 4)), longitudeE7: Int32(bitPattern: frame.payload.leUInt32(at: 8)), routeID: frame.payload.leUInt32(at: 12), remainingM: frame.payload.leUInt32(at: 16), nextManeuverM: frame.payload.leUInt32(at: 20), freeHeap: frame.payload.leUInt32(at: 24))
    }
}

enum BikeOSUUID {
    static let service = "A9D30000-9C9A-4F10-9B53-42494B454F53"
    static let deviceInfo = "A9D30001-9C9A-4F10-9B53-42494B454F53"
    static let status = "A9D30002-9C9A-4F10-9B53-42494B454F53"
    static let control = "A9D30003-9C9A-4F10-9B53-42494B454F53"
    static let data = "A9D30004-9C9A-4F10-9B53-42494B454F53"
    static let acknowledgement = "A9D30005-9C9A-4F10-9B53-42494B454F53"
    static let event = "A9D30006-9C9A-4F10-9B53-42494B454F53"
}

extension Data {
    mutating func appendLE<T: FixedWidthInteger>(_ value: T) { var v = value.littleEndian; Swift.withUnsafeBytes(of: &v) { append(contentsOf: $0) } }
    func leUInt16(at offset: Int) -> UInt16 { UInt16(self[offset]) | UInt16(self[offset + 1]) << 8 }
    func leUInt32(at offset: Int) -> UInt32 { UInt32(self[offset]) | UInt32(self[offset + 1]) << 8 | UInt32(self[offset + 2]) << 16 | UInt32(self[offset + 3]) << 24 }
}
