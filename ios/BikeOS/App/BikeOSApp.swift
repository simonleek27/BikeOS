import MapKit
import SwiftUI

@main struct BikeOSApp: App { var body: some Scene { WindowGroup { BikeOSHomeView() } } }

struct BikeOSHomeView: View {
    @StateObject private var bluetooth = BikeOSBluetooth()
    @State private var showingRoute = false
    private let route = BikeOSRoutePlan(name: "Amsterdam demo route", distanceM: 1409.7, durationMin: 8, points: [CLLocationCoordinate2D(latitude: 52.3548, longitude: 4.8775), CLLocationCoordinate2D(latitude: 52.359, longitude: 4.885), CLLocationCoordinate2D(latitude: 52.364, longitude: 4.894)])
    var body: some View {
        NavigationStack { ScrollView { VStack(alignment: .leading, spacing: 18) {
            header
            MapPreview(route: route).frame(height: 280).clipShape(RoundedRectangle(cornerRadius: 22))
            routeCard
            deviceCard
            if showingRoute { NavigationPreview(route: route, status: bluetooth.status) }
        }.padding() }.navigationTitle("BikeOS") }
    }
    private var header: some View { HStack { VStack(alignment: .leading) { Text("Ride smarter").font(.largeTitle.bold()); Text("Phone → ESP32 → LCD").foregroundStyle(.secondary) }; Spacer(); Image(systemName: "bicycle.circle.fill").font(.system(size: 44)).foregroundStyle(.blue) } }
    private var routeCard: some View { VStack(alignment: .leading, spacing: 10) { Label(route.name, systemImage: "map").font(.headline); HStack { Text("1.41 km"); Text("•"); Text("8 min"); Spacer(); Button(showingRoute ? "Hide" : "Preview") { showingRoute.toggle() }.buttonStyle(.borderedProminent) } } .padding().background(.blue.opacity(0.1)).clipShape(RoundedRectangle(cornerRadius: 18)) }
    private var deviceCard: some View { VStack(alignment: .leading, spacing: 12) { HStack { Circle().fill(bluetooth.connection == "Connected" ? .green : .orange).frame(width: 10, height: 10); Text(bluetooth.deviceName).font(.headline); Spacer(); Text(bluetooth.connection).font(.subheadline).foregroundStyle(.secondary) }; HStack { Button("Connect") { bluetooth.connect() }.buttonStyle(.borderedProminent); Button("Send demo route") { bluetooth.sendDemoRoute() }.buttonStyle(.bordered); Button("Disconnect") { bluetooth.disconnect() }.buttonStyle(.bordered) }; Text(bluetooth.lastAck).font(.caption).foregroundStyle(.secondary); if let status = bluetooth.status { Text("\(status.state.label) · \(status.remainingM)m remaining").font(.caption) } } .padding().background(.thinMaterial).clipShape(RoundedRectangle(cornerRadius: 18)) }
}

private struct MapPreview: View { let route: BikeOSRoutePlan; var body: some View { Map(initialPosition: .region(MKCoordinateRegion(center: route.points[1], span: MKCoordinateSpan(latitudeDelta: 0.02, longitudeDelta: 0.03)))) { MapPolyline(coordinates: route.points).stroke(.blue, lineWidth: 5); Marker("Start", coordinate: route.points[0]).tint(.green); Marker("Destination", coordinate: route.points.last!).tint(.red) } } }
private struct NavigationPreview: View { let route: BikeOSRoutePlan; let status: BikeOSStatus?; var body: some View { VStack(alignment: .leading, spacing: 8) { Text("Navigation preview").font(.headline); Text(status?.state.label ?? "Ready to ride").font(.title2.bold()); Text("Next turn in \(status?.nextManeuverM ?? 206)m").foregroundStyle(.secondary) } .padding().frame(maxWidth: .infinity, alignment: .leading).background(.green.opacity(0.12)).clipShape(RoundedRectangle(cornerRadius: 18)) } }
private extension BikeOSNavigationState { var label: String { switch self { case .navigating: "Navigating"; case .destinationReached: "Arrived"; case .phoneDisconnected: "Phone disconnected"; case .offRoute: "Off route"; case .rerouting: "Rerouting"; case .gpsLost: "GPS lost"; case .waitingForGPS: "Waiting for GPS"; case .noRoute: "No route"; case .error: "Error" } } }
