import MapKit
import SwiftUI

@main struct BikeOSApp: App {
    var body: some Scene { WindowGroup { BikeOSHomeView() } }
}

struct BikeOSHomeView: View {
    @StateObject private var bluetooth = BikeOSBluetooth()
    @State private var route: BikeOSRoutePlan?
    @State private var routeError: String?
    @State private var showingNavigation = false

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    header
                    if let route {
                        MapPreview(route: route, status: bluetooth.status)
                            .frame(height: 300)
                            .clipShape(RoundedRectangle(cornerRadius: 22))
                        routeCard(route)
                    } else if let routeError {
                        ContentUnavailableView("Route unavailable", systemImage: "map", description: Text(routeError))
                    } else {
                        ProgressView("Loading bundled BRTE route…")
                            .frame(maxWidth: .infinity, minHeight: 300)
                    }
                    deviceCard
                }
                .padding()
            }
            .navigationTitle("BikeOS")
            .navigationDestination(isPresented: $showingNavigation) {
                if let route { ActiveNavigationView(route: route, bluetooth: bluetooth) }
            }
            .task {
                do { route = try BikeOSRouteDecoder.loadBundledDemo() }
                catch { routeError = error.localizedDescription }
            }
        }
    }

    private var header: some View {
        HStack {
            VStack(alignment: .leading) {
                Text("Ride smarter").font(.largeTitle.bold())
                Text("Phone → ESP32 → color TFT").foregroundStyle(.secondary)
            }
            Spacer()
            Image(systemName: "bicycle.circle.fill")
                .font(.system(size: 44))
                .foregroundStyle(.blue)
        }
    }

    private func routeCard(_ route: BikeOSRoutePlan) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Label(route.name, systemImage: "map").font(.headline)
                Spacer()
                Text(route.source).font(.caption).foregroundStyle(.secondary)
            }
            HStack {
                Label(String(format: "%.2f km", route.distanceM / 1000), systemImage: "point.topleft.down.to.point.bottomright.curvepath")
                Text("•")
                Label("\(route.durationMin) min", systemImage: "clock")
                Spacer()
                Button("Preview") { showingNavigation = true }.buttonStyle(.borderedProminent)
            }
            if let first = route.maneuvers.first {
                Label("First turn: \(first.type.label)\(first.streetName.isEmpty ? "" : " on \(first.streetName)")",
                      systemImage: first.type.symbolName)
                    .font(.subheadline)
            }
        }
        .padding()
        .background(.blue.opacity(0.1))
        .clipShape(RoundedRectangle(cornerRadius: 18))
    }

    private var deviceCard: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Circle()
                    .fill(bluetooth.connection == "Connected" ? .green : .orange)
                    .frame(width: 10, height: 10)
                Text(bluetooth.deviceName).font(.headline)
                Spacer()
                Text(bluetooth.connection).font(.subheadline).foregroundStyle(.secondary)
            }
            HStack {
                Button("Connect") { bluetooth.connect() }.buttonStyle(.borderedProminent)
                Button("Send demo route") { bluetooth.sendDemoRoute() }.buttonStyle(.bordered)
                Button("Disconnect") { bluetooth.disconnect() }.buttonStyle(.bordered)
            }
            if let progress = bluetooth.transferProgress, progress < 1 {
                ProgressView(value: progress)
            }
            Text(bluetooth.lastAck).font(.caption).foregroundStyle(.secondary)
            if let status = bluetooth.status {
                Text("\(status.state.label) · \(status.remainingM)m remaining · \(status.gps.label)")
                    .font(.caption)
            }
        }
        .padding()
        .background(.thinMaterial)
        .clipShape(RoundedRectangle(cornerRadius: 18))
    }
}

private struct MapPreview: View {
    let route: BikeOSRoutePlan
    let status: BikeOSStatus?

    var body: some View {
        Map(initialPosition: .region(route.region)) {
            MapPolyline(coordinates: route.points).stroke(.blue, lineWidth: 5)
            Marker("Start", coordinate: route.points[0]).tint(.green)
            Marker("Destination", coordinate: route.points.last!).tint(.red)
            if let status, status.gpsValid {
                Marker("ESP32", coordinate: status.coordinate).tint(.orange)
            }
        }
    }
}

private struct ActiveNavigationView: View {
    @Environment(\.dismiss) private var dismiss
    let route: BikeOSRoutePlan
    @ObservedObject var bluetooth: BikeOSBluetooth
    @State private var cameraPosition: MapCameraPosition
    @State private var followsRider = true
    @State private var headingUp = false
    @State private var suppressCameraChange = false

    init(route: BikeOSRoutePlan, bluetooth: BikeOSBluetooth) {
        self.route = route
        self.bluetooth = bluetooth
        _cameraPosition = State(initialValue: .region(route.region))
    }

    private var currentManeuver: BikeOSManeuver? {
        let matched = Int(bluetooth.status?.matchedPointIndex ?? 0)
        return route.maneuvers.first(where: { $0.pointIndex >= matched }) ?? route.maneuvers.last
    }

    private var maneuverDistance: UInt32 {
        bluetooth.status?.nextManeuverM ?? UInt32(currentManeuver?.distanceM ?? 0)
    }

    var body: some View {
        ZStack(alignment: .top) {
            Map(position: $cameraPosition, interactionModes: [.pan, .zoom, .rotate]) {
                MapPolyline(coordinates: route.points).stroke(.blue, lineWidth: 5)
                Marker("Start", coordinate: route.points[0]).tint(.green)
                Marker("Destination", coordinate: route.points.last!).tint(.red)
                if let coordinate = bluetooth.liveCoordinate {
                    Annotation("You", coordinate: coordinate, anchor: .center) {
                        Image(systemName: "location.north.fill")
                            .font(.title2)
                            .foregroundStyle(.orange)
                            .rotationEffect(.degrees(bluetooth.liveHeading ?? 0))
                            .padding(8)
                            .background(.white)
                            .clipShape(Circle())
                            .shadow(radius: 3)
                    }
                }
            }
            .ignoresSafeArea(edges: .top)
            .onMapCameraChange(frequency: .onEnd) { _ in
                if !suppressCameraChange { followsRider = false }
            }
            .onChange(of: bluetooth.liveCoordinate?.latitude) { _, _ in followRiderIfNeeded() }
            .onChange(of: bluetooth.liveHeading) { _, _ in followRiderIfNeeded() }

            VStack(spacing: 10) {
                HStack {
                    Label("SIMULATED GPS", systemImage: "location.fill")
                        .font(.caption.bold())
                        .padding(.horizontal, 10)
                        .padding(.vertical, 7)
                        .background(.orange)
                        .foregroundStyle(.white)
                        .clipShape(Capsule())
                    Spacer()
                    Text(bluetooth.connection)
                        .font(.caption.bold())
                        .padding(.horizontal, 10)
                        .padding(.vertical, 7)
                        .background(.thinMaterial)
                        .clipShape(Capsule())
                }
                .padding(.horizontal)

                HStack(spacing: 8) {
                    Button("Recenter") { recenter() }.buttonStyle(.borderedProminent)
                    Button("Route fit") { fitRoute() }.buttonStyle(.bordered)
                    Toggle("Heading", isOn: $headingUp)
                        .toggleStyle(.button)
                        .onChange(of: headingUp) { _, _ in recenter() }
                }
                .padding(.horizontal)
            }

            VStack {
                Spacer()
                informationCard
            }
        }
        .navigationTitle("Navigation")
        .navigationBarTitleDisplayMode(.inline)
        .onAppear { fitRoute() }
    }

    private var informationCard: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Image(systemName: currentManeuver?.type.symbolName ?? "arrow.up")
                    .font(.title)
                VStack(alignment: .leading) {
                    Text(currentManeuver?.type.label ?? "Follow the route").font(.headline)
                    Text(currentManeuver?.streetName.isEmpty == false ? currentManeuver!.streetName : "Live ESP32 telemetry")
                        .foregroundStyle(.secondary)
                }
                Spacer()
                VStack(alignment: .trailing) {
                    Text("\(maneuverDistance) m").font(.title3.bold())
                    Text("next turn").font(.caption).foregroundStyle(.secondary)
                }
            }
            Divider()
            HStack {
                Label(bluetooth.status?.state.label ?? "Ready", systemImage: "dot.radiowaves.left.and.right")
                Spacer()
                Text("\(bluetooth.status?.remainingM ?? UInt32(route.distanceM)) m left")
            }
            HStack {
                Button("Send route") { bluetooth.sendDemoRoute() }.buttonStyle(.bordered)
                Button("Start") { bluetooth.startNavigation() }.buttonStyle(.borderedProminent)
                Button("Pause") { bluetooth.pauseNavigation() }.buttonStyle(.bordered)
                Button("Resume") { bluetooth.resumeNavigation() }.buttonStyle(.bordered)
            }
            HStack {
                Button("Restart") { bluetooth.restartNavigation() }.buttonStyle(.bordered)
                Button("Test off-route") { bluetooth.triggerOffRoute() }.buttonStyle(.bordered)
                Spacer()
                Button("End ride") { bluetooth.stopNavigation(); dismiss() }.buttonStyle(.bordered)
            }
            Text(bluetooth.lastAck).font(.caption).foregroundStyle(.secondary)
        }
        .padding()
        .background(.regularMaterial)
    }

    private func recenter() {
        followsRider = true
        followRiderIfNeeded()
    }

    private func fitRoute() {
        suppressCameraChange = true
        cameraPosition = .region(route.region)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { suppressCameraChange = false }
    }

    private func followRiderIfNeeded() {
        guard followsRider, let coordinate = bluetooth.liveCoordinate else { return }
        suppressCameraChange = true
        cameraPosition = .camera(MapCamera(centerCoordinate: coordinate,
                                            distance: 850,
                                            heading: headingUp ? (bluetooth.liveHeading ?? 0) : 0,
                                            pitch: 0))
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { suppressCameraChange = false }
    }
}

private extension BikeOSRoutePlan {
    var region: MKCoordinateRegion {
        guard let first = points.first else { return MKCoordinateRegion() }
        let lats = points.map(\.latitude)
        let lons = points.map(\.longitude)
        let minLat = lats.min() ?? first.latitude
        let maxLat = lats.max() ?? first.latitude
        let minLon = lons.min() ?? first.longitude
        let maxLon = lons.max() ?? first.longitude
        return MKCoordinateRegion(
            center: CLLocationCoordinate2D(latitude: (minLat + maxLat) / 2,
                                           longitude: (minLon + maxLon) / 2),
            span: MKCoordinateSpan(latitudeDelta: max(0.005, (maxLat - minLat) * 1.35),
                                   longitudeDelta: max(0.005, (maxLon - minLon) * 1.35)))
    }
}

private extension BikeOSGpsSource {
    var label: String {
        switch self { case .simulated: "simulated"; case .uart: "NEO-6M"; case .iPhoneFallback: "iPhone"; case .none: "none" }
    }
}

private extension BikeOSNavigationState {
    var label: String {
        switch self {
        case .navigating: "Navigating"; case .destinationReached: "Arrived";
        case .phoneDisconnected: "Phone disconnected"; case .offRoute: "Off route";
        case .rerouting: "Rerouting"; case .gpsLost: "GPS lost";
        case .waitingForGPS: "Waiting for GPS"; case .noRoute: "No route"; case .error: "Error"
        }
    }
}
