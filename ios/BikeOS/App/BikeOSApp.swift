import MapKit
import SwiftUI

@main struct BikeOSApp: App { var body: some Scene { WindowGroup { BikeOSHomeView() } } }

struct BikeOSHomeView: View {
    @StateObject private var bluetooth = BikeOSBluetooth()
    @State private var route: BikeOSRoutePlan?
    @State private var routeError: String?
    @State private var showingNavigation = false

    var body: some View {
        NavigationStack {
            ScrollView { VStack(alignment: .leading, spacing: 18) {
                header
                if let route { MapPreview(route: route, status: bluetooth.status).frame(height: 300).clipShape(RoundedRectangle(cornerRadius: 22)); routeCard(route) } else if let routeError { ContentUnavailableView("Route unavailable", systemImage: "map", description: Text(routeError)) } else { ProgressView("Loading bundled BRTE route…").frame(maxWidth: .infinity, minHeight: 300) }
                deviceCard
            }.padding() }
            .navigationTitle("BikeOS")
            .navigationDestination(isPresented: $showingNavigation) { if let route { ActiveNavigationView(route: route, bluetooth: bluetooth) } }
            .task { do { route = try BikeOSRouteDecoder.loadBundledDemo() } catch { routeError = error.localizedDescription } }
        }
    }

    private var header: some View { HStack { VStack(alignment: .leading) { Text("Ride smarter").font(.largeTitle.bold()); Text("Phone → ESP32 → LCD").foregroundStyle(.secondary) }; Spacer(); Image(systemName: "bicycle.circle.fill").font(.system(size: 44)).foregroundStyle(.blue) } }

    private func routeCard(_ route: BikeOSRoutePlan) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack { Label(route.name, systemImage: "map").font(.headline); Spacer(); Text(route.source).font(.caption).foregroundStyle(.secondary) }
            HStack { Label(String(format: "%.2f km", route.distanceM / 1000), systemImage: "point.topleft.down.to.point.bottomright.curvepath"); Text("•"); Label("\(route.durationMin) min", systemImage: "clock"); Spacer(); Button("Preview") { showingNavigation = true }.buttonStyle(.borderedProminent) }
            if let first = route.maneuvers.first { Label("Next: \(first.type.label)\(first.streetName.isEmpty ? "" : " on \(first.streetName)")", systemImage: first.type.symbolName).font(.subheadline) }
        }.padding().background(.blue.opacity(0.1)).clipShape(RoundedRectangle(cornerRadius: 18))
    }

    private var deviceCard: some View { VStack(alignment: .leading, spacing: 12) { HStack { Circle().fill(bluetooth.connection == "Connected" ? .green : .orange).frame(width: 10, height: 10); Text(bluetooth.deviceName).font(.headline); Spacer(); Text(bluetooth.connection).font(.subheadline).foregroundStyle(.secondary) }; HStack { Button("Connect") { bluetooth.connect() }.buttonStyle(.borderedProminent); Button("Send demo route") { bluetooth.sendDemoRoute() }.buttonStyle(.bordered); Button("Disconnect") { bluetooth.disconnect() }.buttonStyle(.bordered) }; Text(bluetooth.lastAck).font(.caption).foregroundStyle(.secondary); if let status = bluetooth.status { Text("\(status.state.label) · \(status.remainingM)m remaining").font(.caption) } } .padding().background(.thinMaterial).clipShape(RoundedRectangle(cornerRadius: 18)) }
}

private struct MapPreview: View {
    let route: BikeOSRoutePlan; let status: BikeOSStatus?
    var body: some View { Map(initialPosition: .region(route.region)) { MapPolyline(coordinates: route.points).stroke(.blue, lineWidth: 5); Marker("Start", coordinate: route.points[0]).tint(.green); Marker("Destination", coordinate: route.points.last!).tint(.red); if let status, status.latitudeE7 != 0 { Marker("You", coordinate: status.coordinate).tint(.orange) } } }
}

private struct ActiveNavigationView: View {
    let route: BikeOSRoutePlan; @ObservedObject var bluetooth: BikeOSBluetooth
    var body: some View { VStack(spacing: 0) { Map(initialPosition: .region(route.region)) { MapPolyline(coordinates: route.points).stroke(.blue, lineWidth: 5); Marker("Destination", coordinate: route.points.last!).tint(.red); if let status = bluetooth.status, status.latitudeE7 != 0 { Annotation("You", coordinate: status.coordinate) { Image(systemName: "location.fill").foregroundStyle(.orange).padding(8).background(.white).clipShape(Circle()).shadow(radius: 3) } } }.ignoresSafeArea(edges: .top); VStack(alignment: .leading, spacing: 8) { HStack { Image(systemName: nextManeuver?.type.symbolName ?? "arrow.up").font(.title); VStack(alignment: .leading) { Text(nextManeuver?.type.label ?? "Follow the route").font(.headline); Text(nextManeuver?.streetName ?? "Live ESP32 telemetry").foregroundStyle(.secondary) }; Spacer(); Text("\(bluetooth.status?.nextManeuverM ?? UInt32(nextManeuver?.distanceM ?? 0)) m").font(.title3.bold()) }; Divider(); HStack { Label(bluetooth.status?.state.label ?? "Ready", systemImage: "dot.radiowaves.left.and.right"); Spacer(); Text("\(bluetooth.status?.remainingM ?? UInt32(route.distanceM)) m left") }; Text(bluetooth.lastAck).font(.caption).foregroundStyle(.secondary) }.padding().background(.regularMaterial) } .navigationTitle("Navigation").navigationBarTitleDisplayMode(.inline) }
    private var nextManeuver: BikeOSManeuver? { route.maneuvers.first }
}

private extension BikeOSRoutePlan {
    var region: MKCoordinateRegion { guard let first = points.first else { return MKCoordinateRegion() }; let lats = points.map(\.latitude); let lons = points.map(\.longitude); let minLat = lats.min() ?? first.latitude; let maxLat = lats.max() ?? first.latitude; let minLon = lons.min() ?? first.longitude; let maxLon = lons.max() ?? first.longitude; return MKCoordinateRegion(center: CLLocationCoordinate2D(latitude: (minLat + maxLat) / 2, longitude: (minLon + maxLon) / 2), span: MKCoordinateSpan(latitudeDelta: max(0.005, (maxLat - minLat) * 1.35), longitudeDelta: max(0.005, (maxLon - minLon) * 1.35))) }
}

private extension BikeOSStatus {
    var coordinate: CLLocationCoordinate2D { CLLocationCoordinate2D(latitude: Double(latitudeE7) * 1e-7, longitude: Double(longitudeE7) * 1e-7) }
}

private extension BikeOSNavigationState { var label: String { switch self { case .navigating: "Navigating"; case .destinationReached: "Arrived"; case .phoneDisconnected: "Phone disconnected"; case .offRoute: "Off route"; case .rerouting: "Rerouting"; case .gpsLost: "GPS lost"; case .waitingForGPS: "Waiting for GPS"; case .noRoute: "No route"; case .error: "Error" } } }
