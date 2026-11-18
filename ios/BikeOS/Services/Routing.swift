import CoreLocation
import Foundation

enum BikeOSManeuverType: UInt8 {
    case straight = 0, left, right, slightLeft, slightRight, sharpLeft, sharpRight, uTurn, roundabout, destination
    var label: String {
        switch self { case .straight: "Continue straight"; case .left: "Turn left"; case .right: "Turn right"; case .slightLeft: "Bear left"; case .slightRight: "Bear right"; case .sharpLeft: "Sharp left"; case .sharpRight: "Sharp right"; case .uTurn: "U-turn"; case .roundabout: "Roundabout"; case .destination: "Arrive at destination" }
    }
    var symbolName: String {
        switch self { case .left, .slightLeft, .sharpLeft: "arrow.turn.up.left"; case .right, .slightRight, .sharpRight: "arrow.turn.up.right"; case .uTurn: "arrow.uturn.backward"; case .roundabout: "arrow.triangle.2.circlepath"; case .destination: "flag.checkered"; case .straight: "arrow.up" }
    }
}

struct BikeOSManeuver: Identifiable {
    let id: Int
    let pointIndex: Int
    let type: BikeOSManeuverType
    let streetName: String
    let distanceM: Double
}

struct BikeOSRoutePlan: Identifiable {
    let id: UInt32
    let name: String
    let distanceM: Double
    let durationMin: Int
    let points: [CLLocationCoordinate2D]
    let maneuvers: [BikeOSManeuver]
    let source: String
    let cumulativeDistanceM: [Double]

    var routeID: UInt32 { id }

    init(id: UInt32, name: String, points: [CLLocationCoordinate2D], maneuvers: [BikeOSManeuver] = [], source: String = "Unknown", durationMin: Int? = nil) {
        self.id = id; self.name = name; self.points = points; self.maneuvers = maneuvers; self.source = source
        var cumulative = [Double](repeating: 0, count: points.count)
        if points.count > 1 { for index in 1..<points.count { let a = CLLocation(latitude: points[index - 1].latitude, longitude: points[index - 1].longitude); let b = CLLocation(latitude: points[index].latitude, longitude: points[index].longitude); cumulative[index] = cumulative[index - 1] + a.distance(from: b) } }
        cumulativeDistanceM = cumulative; distanceM = cumulative.last ?? 0
        self.durationMin = durationMin ?? max(1, Int(ceil(distanceM / (15.0 * 1000.0 / 60.0))))
    }
}

enum BikeOSRouteDecodeError: LocalizedError {
    case missingAsset, invalidHeader, unsupportedVersion, invalidPayload, checksumMismatch, invalidManeuver
    var errorDescription: String? {
        switch self { case .missingAsset: "The bundled route file is missing."; case .invalidHeader: "The route header is invalid."; case .unsupportedVersion: "This route uses an unsupported BRTE version."; case .invalidPayload: "The route payload length or bounds are invalid."; case .checksumMismatch: "The route checksum does not match."; case .invalidManeuver: "A route maneuver record is invalid." }
    }
}

enum BikeOSRouteDecoder {
    static func decode(_ data: Data, name: String = "BRTE route", source: String = "BRTE") throws -> BikeOSRoutePlan {
        guard data.count >= 32, data.prefix(4) == Data([0x42, 0x52, 0x54, 0x45]) else { throw BikeOSRouteDecodeError.invalidHeader }
        guard data.leUInt16(at: 4) == 1, data.leUInt16(at: 6) == 0 else { throw BikeOSRouteDecodeError.unsupportedVersion }
        let routeID = data.leUInt32(at: 8); let originLat = Int32(bitPattern: data.leUInt32(at: 12)); let originLon = Int32(bitPattern: data.leUInt32(at: 16)); let pointCount = Int(data.leUInt16(at: 20)); let maneuverCount = Int(data.leUInt16(at: 22)); let payloadSize = Int(data.leUInt32(at: 24)); let expectedCRC = data.leUInt32(at: 28)
        guard pointCount >= 2, payloadSize == data.count - 32 else { throw BikeOSRouteDecodeError.invalidPayload }
        let payload = data.subdata(in: 32..<data.count); guard BikeOSFrame.crc32(payload) == expectedCRC else { throw BikeOSRouteDecodeError.checksumMismatch }
        var offset = 0; var points = [CLLocationCoordinate2D](); points.reserveCapacity(pointCount)
        for _ in 0..<pointCount { guard offset + 4 <= payload.count else { throw BikeOSRouteDecodeError.invalidPayload }; let latOffset = Int16(bitPattern: payload.leUInt16(at: offset)); offset += 2; let lonOffset = Int16(bitPattern: payload.leUInt16(at: offset)); offset += 2; points.append(CLLocationCoordinate2D(latitude: Double(originLat + Int32(latOffset) * 10) * 1e-7, longitude: Double(originLon + Int32(lonOffset) * 10) * 1e-7)) }
        var maneuvers = [BikeOSManeuver](); maneuvers.reserveCapacity(maneuverCount)
        for index in 0..<maneuverCount { guard offset + 4 <= payload.count else { throw BikeOSRouteDecodeError.invalidManeuver }; let pointIndex = Int(payload.leUInt16(at: offset)); offset += 2; guard pointIndex < points.count, let type = BikeOSManeuverType(rawValue: payload[offset]) else { throw BikeOSRouteDecodeError.invalidManeuver }; offset += 1; let nameLength = Int(payload[offset]); offset += 1; guard nameLength <= 96, offset + nameLength <= payload.count else { throw BikeOSRouteDecodeError.invalidManeuver }; let street = String(data: payload.subdata(in: offset..<(offset + nameLength)), encoding: .utf8) ?? ""; offset += nameLength; maneuvers.append(BikeOSManeuver(id: index, pointIndex: pointIndex, type: type, streetName: street, distanceM: 0)) }
        guard offset == payload.count else { throw BikeOSRouteDecodeError.invalidPayload }
        let base = BikeOSRoutePlan(id: routeID, name: name, points: points, source: source)
        let enriched = maneuvers.map { BikeOSManeuver(id: $0.id, pointIndex: $0.pointIndex, type: $0.type, streetName: $0.streetName, distanceM: base.cumulativeDistanceM[safe: $0.pointIndex] ?? base.distanceM) }
        return BikeOSRoutePlan(id: routeID, name: name, points: points, maneuvers: enriched, source: source)
    }
    static func loadBundledDemo() throws -> BikeOSRoutePlan { guard let url = Bundle.main.url(forResource: "amsterdam_demo", withExtension: "brte"), let data = try? Data(contentsOf: url) else { throw BikeOSRouteDecodeError.missingAsset }; return try decode(data, name: "Amsterdam demo route", source: "Bundled BRTE") }
}

protocol BikeOSRoutingService { func route(from: CLLocationCoordinate2D, to: CLLocationCoordinate2D) async throws -> BikeOSRoutePlan }

struct MockRoutingService: BikeOSRoutingService {
    func route(from: CLLocationCoordinate2D, to: CLLocationCoordinate2D) async throws -> BikeOSRoutePlan { let mid = CLLocationCoordinate2D(latitude: (from.latitude + to.latitude) / 2 + 0.001, longitude: (from.longitude + to.longitude) / 2); return BikeOSRoutePlan(id: 0xD3A00001, name: "Development mock route", points: [from, mid, to], source: "Mock (development only)") }
}

struct GraphHopperRoutingService: BikeOSRoutingService {
    let baseURL: URL; let apiKey: String?
    func route(from: CLLocationCoordinate2D, to: CLLocationCoordinate2D) async throws -> BikeOSRoutePlan {
        guard let apiKey, !apiKey.isEmpty else { throw URLError(.userAuthenticationRequired) }
        var components = URLComponents(url: baseURL, resolvingAgainstBaseURL: false)!; components.queryItems = [URLQueryItem(name: "point", value: "\(from.latitude),\(from.longitude)"), URLQueryItem(name: "point", value: "\(to.latitude),\(to.longitude)"), URLQueryItem(name: "key", value: apiKey), URLQueryItem(name: "type", value: "json"), URLQueryItem(name: "profile", value: "bike"), URLQueryItem(name: "points_encoded", value: "false"), URLQueryItem(name: "instructions", value: "true")]
        let (data, response) = try await URLSession.shared.data(from: components.url!); guard (response as? HTTPURLResponse)?.statusCode == 200 else { throw URLError(.badServerResponse) }
        let decoded = try JSONDecoder().decode(GraphHopperResponse.self, from: data); guard let path = decoded.paths.first, path.points.coordinates.count >= 2 else { throw URLError(.cannotParseResponse) }
        let points = path.points.coordinates.compactMap { pair -> CLLocationCoordinate2D? in guard pair.count >= 2 else { return nil }; return CLLocationCoordinate2D(latitude: pair[1], longitude: pair[0]) }; let routeID = BikeOSFrame.crc32(data)
        let maneuvers = path.instructions.enumerated().compactMap { index, instruction -> BikeOSManeuver? in guard let type = BikeOSManeuverType(graphHopperSign: instruction.sign), let pointIndex = instruction.interval.first else { return nil }; return BikeOSManeuver(id: index, pointIndex: min(pointIndex, points.count - 1), type: type, streetName: instruction.streetName ?? "", distanceM: instruction.distance) }
        return BikeOSRoutePlan(id: routeID, name: "Bike route", points: points, maneuvers: maneuvers, source: "GraphHopper", durationMin: max(1, Int(ceil(path.time / 60000))))
    }
}

private struct GraphHopperResponse: Decodable { let paths: [GraphHopperPath] }
private struct GraphHopperPath: Decodable { let time: Double; let points: GraphHopperPoints; let instructions: [GraphHopperInstruction] }
private struct GraphHopperPoints: Decodable { let coordinates: [[Double]] }
private struct GraphHopperInstruction: Decodable { let distance: Double; let sign: Int; let interval: [Int]; let streetName: String?; enum CodingKeys: String, CodingKey { case distance, sign, interval; case streetName = "street_name" } }

private extension BikeOSManeuverType {
    init?(graphHopperSign: Int) { switch graphHopperSign { case -98: self = .uTurn; case -3: self = .sharpLeft; case -2: self = .left; case -1: self = .slightLeft; case 0: self = .straight; case 1: self = .slightRight; case 2: self = .right; case 3: self = .sharpRight; case 6: self = .roundabout; case 4, 5, 7: self = .destination; default: return nil } }
}

private extension Array { subscript(safe index: Index) -> Element? { indices.contains(index) ? self[index] : nil } }
