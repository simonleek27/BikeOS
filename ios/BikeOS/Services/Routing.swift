import CoreLocation
import Foundation

struct BikeOSRoutePlan { let name: String; let distanceM: Double; let durationMin: Int; let points: [CLLocationCoordinate2D] }

protocol BikeOSRoutingService { func route(from: CLLocationCoordinate2D, to: CLLocationCoordinate2D) async throws -> BikeOSRoutePlan }

struct MockRoutingService: BikeOSRoutingService {
    func route(from: CLLocationCoordinate2D, to: CLLocationCoordinate2D) async throws -> BikeOSRoutePlan {
        let mid = CLLocationCoordinate2D(latitude: (from.latitude + to.latitude) / 2 + 0.001, longitude: (from.longitude + to.longitude) / 2)
        return BikeOSRoutePlan(name: "Demo Amsterdam loop", distanceM: 1409.7, durationMin: 8, points: [from, mid, to])
    }
}

struct GraphHopperRoutingService: BikeOSRoutingService {
    let baseURL: URL
    let apiKey: String?
    func route(from: CLLocationCoordinate2D, to: CLLocationCoordinate2D) async throws -> BikeOSRoutePlan {
        // The network adapter is intentionally explicit: keep credentials in an xcconfig, never in source control.
        guard let apiKey, !apiKey.isEmpty else { throw URLError(.userAuthenticationRequired) }
        var components = URLComponents(url: baseURL, resolvingAgainstBaseURL: false)!; components.queryItems = [URLQueryItem(name: "point", value: "\(from.latitude),\(from.longitude)"), URLQueryItem(name: "point", value: "\(to.latitude),\(to.longitude)"), URLQueryItem(name: "key", value: apiKey), URLQueryItem(name: "type", value: "json")]
        _ = try await URLSession.shared.data(from: components.url!); throw URLError(.cannotParseResponse)
    }
}
