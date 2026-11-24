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
    case missingAsset, invalidHeader, unsupportedVersion, invalidPayload, checksumMismatch, invalidManeuver, unsupportedGeometry
    var errorDescription: String? {
        switch self { case .missingAsset: "The bundled route file is missing."; case .invalidHeader: "The route header is invalid."; case .unsupportedVersion: "This route uses an unsupported BRTE version."; case .invalidPayload: "The route payload length or bounds are invalid."; case .checksumMismatch: "The route checksum does not match."; case .invalidManeuver: "A route maneuver record is invalid."; case .unsupportedGeometry: "This route is too large for the ESP32 route format. Choose a shorter route or split the trip." }
    }
}

enum BikeOSRouteDecoder {
    static func decode(_ data: Data, name: String = "BRTE route", source: String = "BRTE") throws -> BikeOSRoutePlan {
        guard data.count >= 32, data.prefix(4) == Data([0x42, 0x52, 0x54, 0x45]) else { throw BikeOSRouteDecodeError.invalidHeader }
        let version = data.leUInt16(at: 4)
        guard version == 1 || version == 2, data.leUInt16(at: 6) == 0 else { throw BikeOSRouteDecodeError.unsupportedVersion }
        let routeID = data.leUInt32(at: 8); let originLat = Int32(bitPattern: data.leUInt32(at: 12)); let originLon = Int32(bitPattern: data.leUInt32(at: 16)); let pointCount = Int(data.leUInt16(at: 20)); let maneuverCount = Int(data.leUInt16(at: 22)); let payloadSize = Int(data.leUInt32(at: 24)); let expectedCRC = data.leUInt32(at: 28)
        guard pointCount >= 2, payloadSize == data.count - 32, data.count <= 512 * 1024 else { throw BikeOSRouteDecodeError.invalidPayload }
        let payload = data.subdata(in: 32..<data.count); guard BikeOSFrame.crc32(payload) == expectedCRC else { throw BikeOSRouteDecodeError.checksumMismatch }
        var offset = 0; var points = [CLLocationCoordinate2D](); points.reserveCapacity(pointCount)
        for _ in 0..<pointCount {
            if version == 1 {
                guard offset + 4 <= payload.count else { throw BikeOSRouteDecodeError.invalidPayload }
                let latOffset = Int16(bitPattern: payload.leUInt16(at: offset)); offset += 2
                let lonOffset = Int16(bitPattern: payload.leUInt16(at: offset)); offset += 2
                points.append(CLLocationCoordinate2D(latitude: Double(originLat + Int32(latOffset) * 10) * 1e-7, longitude: Double(originLon + Int32(lonOffset) * 10) * 1e-7))
            } else {
                guard offset + 8 <= payload.count else { throw BikeOSRouteDecodeError.invalidPayload }
                let lat = Int32(bitPattern: payload.leUInt32(at: offset)); offset += 4
                let lon = Int32(bitPattern: payload.leUInt32(at: offset)); offset += 4
                let coordinate = CLLocationCoordinate2D(latitude: Double(lat) * 1e-7, longitude: Double(lon) * 1e-7)
                guard (-90...90).contains(coordinate.latitude), (-180...180).contains(coordinate.longitude) else { throw BikeOSRouteDecodeError.invalidPayload }
                points.append(coordinate)
            }
        }
        var maneuvers = [BikeOSManeuver](); maneuvers.reserveCapacity(maneuverCount)
        for index in 0..<maneuverCount { guard offset + 4 <= payload.count else { throw BikeOSRouteDecodeError.invalidManeuver }; let pointIndex = Int(payload.leUInt16(at: offset)); offset += 2; guard pointIndex < points.count, let type = BikeOSManeuverType(rawValue: payload[offset]) else { throw BikeOSRouteDecodeError.invalidManeuver }; offset += 1; let nameLength = Int(payload[offset]); offset += 1; guard nameLength <= 96, offset + nameLength <= payload.count else { throw BikeOSRouteDecodeError.invalidManeuver }; let street = String(data: payload.subdata(in: offset..<(offset + nameLength)), encoding: .utf8) ?? ""; offset += nameLength; maneuvers.append(BikeOSManeuver(id: index, pointIndex: pointIndex, type: type, streetName: street, distanceM: 0)) }
        guard offset == payload.count else { throw BikeOSRouteDecodeError.invalidPayload }
        let base = BikeOSRoutePlan(id: routeID, name: name, points: points, source: source)
        let enriched = maneuvers.map { BikeOSManeuver(id: $0.id, pointIndex: $0.pointIndex, type: $0.type, streetName: $0.streetName, distanceM: base.cumulativeDistanceM[safe: $0.pointIndex] ?? base.distanceM) }
        return BikeOSRoutePlan(id: routeID, name: name, points: points, maneuvers: enriched, source: source)
    }
    static func loadBundledDemo() throws -> BikeOSRoutePlan { guard let url = Bundle.main.url(forResource: "amsterdam_demo", withExtension: "brte"), let data = try? Data(contentsOf: url) else { throw BikeOSRouteDecodeError.missingAsset }; return try decode(data, name: "Amsterdam demo route", source: "Bundled BRTE") }
}

enum BikeOSRouteEncoder {
    static func encode(_ route: BikeOSRoutePlan) throws -> Data {
        guard route.points.count >= 2, route.points.count <= Int(UInt16.max),
              route.maneuvers.count <= Int(UInt16.max) else { throw BikeOSRouteDecodeError.unsupportedGeometry }
        guard route.points.allSatisfy({ $0.latitude.isFinite && $0.longitude.isFinite &&
                                        (-90.0...90.0).contains($0.latitude) &&
                                        (-180.0...180.0).contains($0.longitude) }) else {
            throw BikeOSRouteDecodeError.unsupportedGeometry
        }
        let originLat = Int64((route.points[0].latitude * 10_000_000).rounded())
        let originLon = Int64((route.points[0].longitude * 10_000_000).rounded())
        let fitsV1 = route.points.allSatisfy { point in
            let lat = Int64((point.latitude * 10_000_000).rounded()) - originLat
            let lon = Int64((point.longitude * 10_000_000).rounded()) - originLon
            return abs(lat) <= Int64(Int16.max) * 10 && abs(lon) <= Int64(Int16.max) * 10
        }
        var payload = Data()
        if fitsV1 {
            for point in route.points {
                let lat = Int64((point.latitude * 10_000_000).rounded()) - originLat
                let lon = Int64((point.longitude * 10_000_000).rounded()) - originLon
                payload.appendLE(Int16(max(Int64(Int16.min), min(Int64(Int16.max), lat / 10))))
                payload.appendLE(Int16(max(Int64(Int16.min), min(Int64(Int16.max), lon / 10))))
            }
        }
        func appendManeuvers() throws {
            for maneuver in route.maneuvers {
                guard maneuver.pointIndex >= 0, maneuver.pointIndex < route.points.count else { throw BikeOSRouteDecodeError.invalidManeuver }
                let name = Data(maneuver.streetName.utf8)
                guard name.count <= 96 else { throw BikeOSRouteDecodeError.invalidManeuver }
                payload.appendLE(UInt16(maneuver.pointIndex)); payload.append(maneuver.type.rawValue); payload.append(UInt8(name.count)); payload.append(name)
            }
        }
        try appendManeuvers()
        if !fitsV1 {
            payload = Data()
            for point in route.points {
                payload.appendLE(Int32((point.latitude * 10_000_000).rounded()))
                payload.appendLE(Int32((point.longitude * 10_000_000).rounded()))
            }
            try appendManeuvers()
        }
        guard payload.count + 32 <= 512 * 1024 else { throw BikeOSRouteDecodeError.unsupportedGeometry }
        var output = Data([0x42, 0x52, 0x54, 0x45])
        output.appendLE(UInt16(fitsV1 ? 1 : 2)); output.appendLE(UInt16(0)); output.appendLE(route.routeID)
        output.appendLE(Int32(originLat)); output.appendLE(Int32(originLon))
        output.appendLE(UInt16(route.points.count)); output.appendLE(UInt16(route.maneuvers.count))
        output.appendLE(UInt32(payload.count)); output.appendLE(BikeOSFrame.crc32(payload)); output.append(payload)
        return output
    }
}

protocol BikeOSRoutingService { func route(from: CLLocationCoordinate2D, to: CLLocationCoordinate2D) async throws -> BikeOSRoutePlan }

struct MockRoutingService: BikeOSRoutingService {
    func route(from: CLLocationCoordinate2D, to: CLLocationCoordinate2D) async throws -> BikeOSRoutePlan { let mid = CLLocationCoordinate2D(latitude: (from.latitude + to.latitude) / 2 + 0.001, longitude: (from.longitude + to.longitude) / 2); return BikeOSRoutePlan(id: 0xD3A00001, name: "Development mock route", points: [from, mid, to], source: "Mock (development only)") }
}

struct GraphHopperRoutingService: BikeOSRoutingService {
    let baseURL: URL; let apiKey: String?
    func route(from: CLLocationCoordinate2D, to: CLLocationCoordinate2D) async throws -> BikeOSRoutePlan {
        guard let apiKey = apiKey?.trimmingCharacters(in: .whitespacesAndNewlines), !apiKey.isEmpty else {
            throw GraphHopperRoutingError.missingAPIKey
        }
        guard baseURL.scheme?.lowercased() == "https", baseURL.host != nil else {
            throw GraphHopperRoutingError.invalidEndpoint(baseURL.absoluteString)
        }
        var components = URLComponents(url: baseURL, resolvingAgainstBaseURL: false)
        components?.queryItems = [
            URLQueryItem(name: "point", value: "\(from.latitude),\(from.longitude)"),
            URLQueryItem(name: "point", value: "\(to.latitude),\(to.longitude)"),
            URLQueryItem(name: "key", value: apiKey),
            URLQueryItem(name: "type", value: "json"),
            URLQueryItem(name: "profile", value: "bike"),
            URLQueryItem(name: "points_encoded", value: "false"),
            URLQueryItem(name: "instructions", value: "true")
        ]
        guard let requestURL = components?.url else { throw GraphHopperRoutingError.invalidEndpoint(baseURL.absoluteString) }
        var request = URLRequest(url: requestURL)
        request.timeoutInterval = 20
        let (data, response) = try await URLSession.shared.data(for: request)
        guard let http = response as? HTTPURLResponse else { throw GraphHopperRoutingError.invalidResponse }
        guard http.statusCode == 200 else {
            let message = (try? JSONDecoder().decode(GraphHopperErrorResponse.self, from: data))?.message
            throw GraphHopperRoutingError.httpStatus(http.statusCode, message)
        }
        let decoded: GraphHopperResponse
        do { decoded = try JSONDecoder().decode(GraphHopperResponse.self, from: data) }
        catch { throw GraphHopperRoutingError.malformedResponse }
        guard let path = decoded.paths.first, path.points.coordinates.count >= 2 else { throw GraphHopperRoutingError.noRoute }
        let points = path.points.coordinates.compactMap { pair -> CLLocationCoordinate2D? in guard pair.count >= 2 else { return nil }; return CLLocationCoordinate2D(latitude: pair[1], longitude: pair[0]) }; let routeID = BikeOSFrame.crc32(data)
        guard points.count >= 2 else { throw GraphHopperRoutingError.noRoute }
        let maneuvers = path.instructions.enumerated().compactMap { index, instruction -> BikeOSManeuver? in guard let type = BikeOSManeuverType(graphHopperSign: instruction.sign), let pointIndex = instruction.interval.first else { return nil }; return BikeOSManeuver(id: index, pointIndex: min(pointIndex, points.count - 1), type: type, streetName: instruction.streetName ?? "", distanceM: instruction.distance) }
        return BikeOSRoutePlan(id: routeID, name: "Bike route", points: points, maneuvers: maneuvers, source: "GraphHopper", durationMin: max(1, Int(ceil(path.time / 60000))))
    }
}

enum GraphHopperRoutingError: LocalizedError {
    case missingAPIKey
    case invalidEndpoint(String)
    case invalidResponse
    case httpStatus(Int, String?)
    case malformedResponse
    case noRoute

    var errorDescription: String? {
        switch self {
        case .missingAPIKey:
            return "GraphHopper API key is missing. Add it to ios/GraphHopper.xcconfig and rebuild the app."
        case .invalidEndpoint(let value):
            return "GraphHopper URL is invalid (\(value)). It must be https://graphhopper.com/api/1/route."
        case .invalidResponse:
            return "GraphHopper returned an invalid HTTP response. Check the iPhone internet connection."
        case .httpStatus(let code, let message):
            return "GraphHopper returned HTTP \(code).\(message.map { " \($0)" } ?? "")"
        case .malformedResponse:
            return "GraphHopper returned data BikeOS could not read. Check the routing API response format."
        case .noRoute:
            return "GraphHopper found no bicycle route between those locations. Choose closer or reachable points."
        }
    }
}

private struct GraphHopperResponse: Decodable { let paths: [GraphHopperPath] }
private struct GraphHopperPath: Decodable { let time: Double; let points: GraphHopperPoints; let instructions: [GraphHopperInstruction] }
private struct GraphHopperPoints: Decodable { let coordinates: [[Double]] }
private struct GraphHopperInstruction: Decodable { let distance: Double; let sign: Int; let interval: [Int]; let streetName: String?; enum CodingKeys: String, CodingKey { case distance, sign, interval; case streetName = "street_name" } }
private struct GraphHopperErrorResponse: Decodable { let message: String? }

private extension BikeOSManeuverType {
    init?(graphHopperSign: Int) { switch graphHopperSign { case -98: self = .uTurn; case -3: self = .sharpLeft; case -2: self = .left; case -1: self = .slightLeft; case 0: self = .straight; case 1: self = .slightRight; case 2: self = .right; case 3: self = .sharpRight; case 6: self = .roundabout; case 4, 5, 7: self = .destination; default: return nil } }
}

private extension Array { subscript(safe index: Index) -> Element? { indices.contains(index) ? self[index] : nil } }
