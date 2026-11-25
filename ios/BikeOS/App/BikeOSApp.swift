import CoreLocation
import MapKit
import SwiftUI

@main struct BikeOSApp: App {
    var body: some Scene { WindowGroup { BikeOSRootView() } }
}

struct BikeOSRootView: View {
    @StateObject private var bluetooth = BikeOSBluetooth()
    @StateObject private var location = BikeOSLocationProvider()
    @State private var enteredApp = false

    var body: some View {
        Group {
            if enteredApp {
                BikeOSHomeView(bluetooth: bluetooth, location: location)
            } else {
                BikeOSConnectView(bluetooth: bluetooth)
            }
        }
        .onChange(of: bluetooth.connectionState) { _, state in
            if state == .readyForTransfer || state == .connected { enteredApp = true }
        }
    }
}

struct BikeOSConnectView: View {
    @ObservedObject var bluetooth: BikeOSBluetooth

    var body: some View {
        VStack(spacing: 24) {
            Spacer()
            Image(systemName: "bicycle.circle.fill")
                .font(.system(size: 72))
                .foregroundStyle(.blue)
            Text("BikeOS").font(.largeTitle.bold())
            Text("Connect your bike computer to start planning and navigating.")
                .font(.body).foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 300)
            ConnectionStatusView(state: bluetooth.connectionState,
                                 deviceName: bluetooth.deviceName,
                                 firmwareInfo: bluetooth.firmwareInfo)
                .frame(maxWidth: 340)
            Button {
                bluetooth.connect()
            } label: {
                Label(bluetooth.connectionState == .scanning || bluetooth.connectionState == .connecting
                      ? "Connecting…" : "Connect", systemImage: "dot.radiowaves.left.and.right")
                    .frame(maxWidth: .infinity, minHeight: 52)
            }
            .buttonStyle(.borderedProminent)
            .contentShape(Rectangle())
            .disabled(bluetooth.connectionState == .scanning || bluetooth.connectionState == .connecting)
            Text("Keep the ESP32 powered on and nearby.")
                .font(.footnote).foregroundStyle(.secondary)
            Spacer()
        }
        .padding(24)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(.systemGroupedBackground))
    }
}

@MainActor final class BikeOSLocationProvider: NSObject, ObservableObject, CLLocationManagerDelegate {
    @Published private(set) var location: CLLocation?
    @Published private(set) var course: CLLocationDirection?
    @Published private(set) var heading: CLLocationDirection?
    private let manager = CLLocationManager()

    override init() {
        super.init()
        manager.delegate = self
        manager.desiredAccuracy = kCLLocationAccuracyBest
        manager.headingFilter = 5
    }

    func start() {
        manager.requestWhenInUseAuthorization()
        manager.startUpdatingLocation()
        if CLLocationManager.headingAvailable() { manager.startUpdatingHeading() }
    }

    nonisolated func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        guard let location = locations.last else { return }
        Task { @MainActor in
            self.location = location
            if location.course >= 0, location.speed >= 0.8 { self.course = location.course }
        }
    }

    nonisolated func locationManager(_ manager: CLLocationManager, didUpdateHeading newHeading: CLHeading) {
        let value = newHeading.trueHeading >= 0 ? newHeading.trueHeading : newHeading.magneticHeading
        guard value >= 0 else { return }
        Task { @MainActor in self.heading = value }
    }
}

struct BikeOSHomeView: View {
    @ObservedObject var bluetooth: BikeOSBluetooth
    @ObservedObject var location: BikeOSLocationProvider
    @State private var route: BikeOSRoutePlan?
    @State private var routeError: String?
    @State private var showingNavigation = false
    @State private var destinationQuery = ""
    @State private var destinationResults: [MKMapItem] = []
    @State private var startQuery = ""
    @State private var startResults: [MKMapItem] = []
    @State private var selectedDestination: MKMapItem?
    @State private var selectedStart: MKMapItem?
    @State private var useCurrentLocation = true
    @State private var isSearching = false
    @State private var isCalculating = false

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    header
                    deviceCard
                    plannerCard
                    if let routeError { errorCard(routeError) }
                    if let route { routeCard(route) }
                    demoCard
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 12)
            }
            .navigationTitle("BikeOS")
            .navigationBarTitleDisplayMode(.inline)
            .navigationDestination(isPresented: $showingNavigation) {
                if let route { ActiveNavigationView(route: route, bluetooth: bluetooth, location: location) }
            }
            .task {
                if route == nil { route = bluetooth.restoredRoute }
                location.start()
            }
            .onChange(of: bluetooth.restoredRoute?.routeID) { _, _ in
                if route == nil { route = bluetooth.restoredRoute }
            }
        }
    }

    private var header: some View {
        HStack(spacing: 12) {
            Image(systemName: "bicycle.circle.fill").font(.system(size: 42)).foregroundStyle(.blue)
            VStack(alignment: .leading, spacing: 2) {
                Text("Ride smarter").font(.title.bold())
                Text("Plan on iPhone · follow on TFT").font(.subheadline).foregroundStyle(.secondary)
            }
            Spacer(minLength: 0)
        }
        .accessibilityElement(children: .combine)
    }

    private var deviceCard: some View {
        VStack(alignment: .leading, spacing: 12) {
            ConnectionStatusView(state: bluetooth.connectionState,
                                 deviceName: bluetooth.deviceName,
                                 firmwareInfo: bluetooth.firmwareInfo)
            HStack(spacing: 10) {
                Button("Connect", systemImage: "dot.radiowaves.left.and.right") { bluetooth.connect() }
                    .buttonStyle(.borderedProminent)
                    .frame(maxWidth: .infinity, minHeight: 44)
                    .contentShape(Rectangle())
                    .disabled(bluetooth.isBluetoothConnected || bluetooth.connectionState == .scanning || bluetooth.connectionState == .connecting)
                Button("Disconnect", systemImage: "xmark.circle") { bluetooth.disconnect() }
                    .buttonStyle(.bordered)
                    .frame(maxWidth: .infinity, minHeight: 44)
                    .contentShape(Rectangle())
                    .disabled(!bluetooth.isBluetoothConnected)
            }
            if let status = bluetooth.status {
                Text("\(status.state.label) · \(status.runState.label) · \(status.gps.label)")
                    .font(.caption).foregroundStyle(.secondary).lineLimit(1)
            }
            Text(bluetooth.lastAck).font(.caption).foregroundStyle(.secondary).lineLimit(2)
        }
        .padding(14)
        .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 16))
    }

    private var plannerCard: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label("Plan a bicycle route", systemImage: "map").font(.headline)
            HStack(spacing: 8) {
                Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
                TextField("Search destination", text: $destinationQuery)
                    .textInputAutocapitalization(.words).submitLabel(.search)
                    .onSubmit { Task { await searchDestination() } }
                Button("Search") { Task { await searchDestination() } }
                    .buttonStyle(.bordered).frame(minHeight: 44)
            }
            .padding(.horizontal, 10)
            .background(.quaternary, in: RoundedRectangle(cornerRadius: 10))
            if isSearching { ProgressView().frame(maxWidth: .infinity) }
            searchResults(destinationResults, selection: $selectedDestination)
            Picker("Start", selection: $useCurrentLocation) {
                Text("Current location").tag(true)
                Text("Choose start").tag(false)
            }
            .pickerStyle(.segmented)
            if useCurrentLocation && location.location == nil {
                Label("Waiting for the iPhone location…", systemImage: "location.slash")
                    .font(.caption).foregroundStyle(.secondary)
            }
            if !useCurrentLocation {
                HStack(spacing: 8) {
                    Image(systemName: "flag").foregroundStyle(.secondary)
                    TextField("Search starting point", text: $startQuery)
                        .textInputAutocapitalization(.words).submitLabel(.search)
                        .onSubmit { Task { await searchStart() } }
                    Button("Search") { Task { await searchStart() } }
                        .buttonStyle(.bordered).frame(minHeight: 44)
                }
                .padding(.horizontal, 10)
                .background(.quaternary, in: RoundedRectangle(cornerRadius: 10))
                searchResults(startResults, selection: $selectedStart)
            }
            Button { Task { await calculateRoute() } } label: {
                Label(isCalculating ? "Calculating…" : "Calculate bike route", systemImage: "bicycle")
                    .frame(maxWidth: .infinity, minHeight: 46)
            }
            .buttonStyle(.borderedProminent).contentShape(Rectangle())
            .disabled(isCalculating || selectedDestination == nil || (!useCurrentLocation && selectedStart == nil))
            Text("Uses GraphHopper's real bicycle profile. The bundled Amsterdam demo remains available without an API key.")
                .font(.caption).foregroundStyle(.secondary)
        }
        .padding(14)
        .background(.blue.opacity(0.08), in: RoundedRectangle(cornerRadius: 16))
    }

    private func searchResults(_ results: [MKMapItem], selection: Binding<MKMapItem?>) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            ForEach(results.indices, id: \.self) { index in
                let item = results[index]
                Button {
                    selection.wrappedValue = item
                    if selection.wrappedValue === selectedDestination { destinationResults = [] } else { startResults = [] }
                } label: {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(item.name ?? "Unknown place").font(.subheadline.weight(.semibold))
                        Text(item.placemark.title ?? "").font(.caption).foregroundStyle(.secondary).lineLimit(1)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading).padding(.vertical, 9).contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                if index < results.count - 1 { Divider() }
            }
        }
    }

    private func routeCard(_ route: BikeOSRoutePlan) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            MapPreview(route: route, status: bluetooth.status)
                .frame(height: 220).clipShape(RoundedRectangle(cornerRadius: 14))
            HStack {
                VStack(alignment: .leading, spacing: 3) {
                    Text(route.name).font(.headline).lineLimit(1)
                    Text("\(route.distanceM / 1000, specifier: "%.1f") km · \(route.durationMin) min · \(route.source)")
                        .font(.subheadline).foregroundStyle(.secondary)
                }
                Spacer(minLength: 8)
                Button(bluetooth.routeIsActive(route) ? "Navigate" : "Send first") {
                    showingNavigation = true
                }
                    .buttonStyle(.borderedProminent).frame(minHeight: 44).contentShape(Rectangle())
                    .disabled(!bluetooth.routeIsActive(route))
            }
            HStack(spacing: 10) {
                Button("Send to BikeOS", systemImage: "arrow.up.circle") { bluetooth.sendRoute(route) }
                    .buttonStyle(.bordered).frame(maxWidth: .infinity, minHeight: 44).contentShape(Rectangle())
                    .disabled(!bluetooth.isReadyForTransfer || bluetooth.isTransferringRoute)
                if let first = route.maneuvers.first { Text(first.type.label).font(.caption).foregroundStyle(.secondary).lineLimit(1) }
            }
            if bluetooth.isTransferringRoute {
                ProgressView("Waiting for ESP32 to activate this route…")
                    .font(.caption)
            } else if !bluetooth.routeIsActive(route) {
                Text("Send this route and wait for the activation confirmation before starting navigation.")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
        .padding(14).background(.thinMaterial, in: RoundedRectangle(cornerRadius: 16))
    }

    private var demoCard: some View {
        Button {
            if route == nil { loadDemoRoute() }
            showingNavigation = true
        } label: {
            Label("Open Amsterdam demo", systemImage: "play.circle")
                .frame(maxWidth: .infinity, minHeight: 44)
        }
        .buttonStyle(.bordered).contentShape(Rectangle())
    }

    private func errorCard(_ message: String) -> some View {
        Label(message, systemImage: "exclamationmark.triangle")
            .font(.subheadline).foregroundStyle(.orange).frame(maxWidth: .infinity, alignment: .leading)
            .padding(14).background(.orange.opacity(0.12), in: RoundedRectangle(cornerRadius: 14))
    }

    private func loadDemoRoute() {
        do { route = try BikeOSRouteDecoder.loadBundledDemo(); routeError = nil }
        catch { routeError = error.localizedDescription }
    }

    private func searchDestination() async {
        guard !destinationQuery.trimmingCharacters(in: .whitespaces).isEmpty else { return }
        isSearching = true; defer { isSearching = false }
        let request = MKLocalSearch.Request(); request.naturalLanguageQuery = destinationQuery
        if let region = regionAround(location.location?.coordinate) { request.region = region }
        do { destinationResults = try await MKLocalSearch(request: request).start().mapItems }
        catch { routeError = "Destination search failed: \(error.localizedDescription)" }
    }

    private func searchStart() async {
        guard !startQuery.trimmingCharacters(in: .whitespaces).isEmpty else { return }
        isSearching = true; defer { isSearching = false }
        let request = MKLocalSearch.Request(); request.naturalLanguageQuery = startQuery
        if let region = regionAround(location.location?.coordinate) { request.region = region }
        do { startResults = try await MKLocalSearch(request: request).start().mapItems }
        catch { routeError = "Start search failed: \(error.localizedDescription)" }
    }

    private func calculateRoute() async {
        guard let destination = selectedDestination?.placemark.coordinate else { return }
        let start: CLLocationCoordinate2D
        if useCurrentLocation, let current = location.location?.coordinate { start = current }
        else if useCurrentLocation {
            location.start()
            routeError = "The iPhone has not provided a location yet. Allow Location access, wait a moment, then tap Calculate again — or choose a manual start in Amsterdam."
            return
        }
        else if let chosen = selectedStart?.placemark.coordinate { start = chosen }
        else { routeError = "Choose a starting point or allow location access."; return }
        isCalculating = true
        routeError = nil
        defer { isCalculating = false }
        let key = configuredValue("GRAPHHOPPER_API_KEY") ?? ProcessInfo.processInfo.environment["GRAPHHOPPER_API_KEY"]
        let baseString = configuredValue("GRAPHHOPPER_BASE_URL") ?? "https://graphhopper.com/api/1/route"
        guard let baseURL = URL(string: baseString) else { routeError = "GraphHopper base URL is invalid."; return }
        do {
            route = try await GraphHopperRoutingService(baseURL: baseURL, apiKey: key).route(from: start, to: destination)
            routeError = nil
        } catch {
            routeError = error.localizedDescription
        }
    }

    private func regionAround(_ coordinate: CLLocationCoordinate2D?) -> MKCoordinateRegion? {
        guard let coordinate else { return nil }
        return MKCoordinateRegion(center: coordinate, span: MKCoordinateSpan(latitudeDelta: 0.12, longitudeDelta: 0.12))
    }

    private func configuredValue(_ key: String) -> String? {
        guard let value = Bundle.main.object(forInfoDictionaryKey: key) as? String,
              !value.isEmpty,
              !value.hasPrefix("$(") else { return nil }
        return value
    }
}

private struct ConnectionStatusView: View {
    let state: BikeOSConnectionState
    let deviceName: String
    let firmwareInfo: String
    private var color: Color {
        switch state { case .connected, .readyForTransfer: .green; case .scanning, .connecting, .reconnecting, .readyToScan: .orange; case .failed: .red; case .bluetoothOff, .disconnected: .gray }
    }
    var body: some View {
        HStack(spacing: 9) {
            Circle().fill(color).frame(width: 11, height: 11)
            VStack(alignment: .leading, spacing: 1) {
                Text(deviceName).font(.headline).lineLimit(1)
                Text(state.label).font(.caption).foregroundStyle(.secondary).lineLimit(1)
            }
            Spacer(minLength: 4)
            if state == .readyForTransfer { Label("Ready", systemImage: "checkmark.circle.fill").font(.caption.weight(.semibold)).foregroundStyle(.green) }
        }
        .frame(maxWidth: .infinity, alignment: .leading).contentShape(Rectangle())
        .accessibilityElement(children: .combine)
        .accessibilityValue(firmwareInfo.isEmpty ? state.label : "\(state.label). \(firmwareInfo)")
    }
}

private struct MapPreview: View {
    let route: BikeOSRoutePlan
    let status: BikeOSStatus?
    var body: some View {
        Map(initialPosition: .region(route.region)) {
            MapPolyline(coordinates: route.points).stroke(.blue, lineWidth: 5)
            if let first = route.points.first { Marker("Start", coordinate: first).tint(.green) }
            if let last = route.points.last { Marker("Destination", coordinate: last).tint(.red) }
            if let status, status.gpsValid { Marker("ESP32", coordinate: status.coordinate).tint(.orange) }
        }
    }
}

private struct ActiveNavigationView: View {
    @Environment(\.dismiss) private var dismiss
    let route: BikeOSRoutePlan
    @ObservedObject var bluetooth: BikeOSBluetooth
    @ObservedObject var location: BikeOSLocationProvider
    @State private var cameraPosition: MapCameraPosition
    @State private var followsRider = true
    @State private var headingUp = false
    @State private var suppressCameraChange = false

    init(route: BikeOSRoutePlan, bluetooth: BikeOSBluetooth, location: BikeOSLocationProvider) {
        self.route = route
        self.bluetooth = bluetooth
        self.location = location
        _cameraPosition = State(initialValue: .region(route.region))
    }

    private var matchedIndex: Int { Int(bluetooth.status?.matchedPointIndex ?? 0) }
    private var currentManeuver: BikeOSManeuver? { route.maneuvers.first(where: { $0.pointIndex >= matchedIndex }) ?? route.maneuvers.last }
    private var maneuverDistance: UInt32 { bluetooth.status?.nextManeuverM ?? UInt32(max(0, currentManeuver?.distanceM ?? 0)) }
    private var runState: BikeOSRunState { bluetooth.status?.runState ?? .notStarted }
    private var primaryTitle: String { switch runState { case .running: "Pause"; case .paused: "Resume"; case .arrived: "Restart"; case .notStarted, .stopped: "Start" } }
    private var primarySymbol: String { runState == .running ? "pause.fill" : "play.fill" }
    private var displayHeading: CLLocationDirection? {
        if let heading = bluetooth.liveHeading,
           (bluetooth.status?.headingIsUsable == true || bluetooth.status?.gps == .simulated) {
            return heading
        }
        return location.course ?? location.heading ?? bluetooth.liveHeading ?? route.bearing(at: matchedIndex)
    }

    var body: some View {
        Map(position: $cameraPosition, interactionModes: [.pan, .zoom, .rotate]) {
            MapPolyline(coordinates: route.points).stroke(.blue, lineWidth: 5)
            if let first = route.points.first { Marker("Start", coordinate: first).tint(.green) }
            if let last = route.points.last { Marker("Destination", coordinate: last).tint(.red) }
            if let coordinate = bluetooth.liveCoordinate {
                Annotation("You", coordinate: coordinate, anchor: .center) {
                    Image(systemName: "location.north.fill").font(.title3).foregroundStyle(.orange)
                        // Map annotations are screen-oriented. Subtract the
                        // camera heading so the arrow stays aligned in
                        // heading-up mode instead of rotating twice.
                        .rotationEffect(.degrees((displayHeading ?? 0) - (headingUp ? (displayHeading ?? 0) : 0)))
                        .padding(7).background(.white)
                        .clipShape(Circle()).shadow(radius: 3)
                }
            }
        }
        .safeAreaInset(edge: .top, spacing: 0) { topOverlay }
        .safeAreaInset(edge: .bottom, spacing: 0) { navigationCard }
        .onMapCameraChange(frequency: .onEnd) { _ in if !suppressCameraChange { followsRider = false } }
        .onChange(of: bluetooth.liveCoordinate?.latitude) { _, _ in followRiderIfNeeded() }
        .onChange(of: bluetooth.liveHeading) { _, _ in followRiderIfNeeded() }
        .onChange(of: location.course) { _, _ in followRiderIfNeeded() }
        .onChange(of: location.heading) { _, _ in followRiderIfNeeded() }
        .navigationTitle(route.name).navigationBarTitleDisplayMode(.inline).onAppear { fitRoute() }
    }

    private var topOverlay: some View {
        HStack(spacing: 8) {
            Label(bluetooth.status?.gps.label.uppercased() ?? "GPS", systemImage: "location.fill")
                .font(.caption.weight(.semibold)).lineLimit(1).fixedSize(horizontal: true, vertical: false)
                .padding(.horizontal, 9).padding(.vertical, 7).background(.orange, in: Capsule()).foregroundStyle(.white)
            Text(bluetooth.status?.runState.label ?? "Ready").font(.caption.weight(.semibold)).lineLimit(1)
                .frame(minWidth: 66, alignment: .center)
                .padding(.horizontal, 9).padding(.vertical, 7).background(.regularMaterial, in: Capsule())
            Spacer(minLength: 0)
            Circle().fill(bluetooth.connectionState == .readyForTransfer || bluetooth.connectionState == .connected ? .green : .orange)
                .frame(width: 10, height: 10).accessibilityLabel("Bluetooth \(bluetooth.connectionState.label)")
            Button { recenter() } label: {
                Image(systemName: "location.fill")
                    .frame(width: 44, height: 44)
                    .contentShape(Rectangle())
            }
                .buttonStyle(.borderedProminent).accessibilityLabel("Recenter on rider")
        }
        .frame(maxWidth: .infinity, minHeight: 56, alignment: .center)
        .padding(.horizontal, 12).background(.regularMaterial)
        .transaction { transaction in transaction.animation = nil }
    }

    private var navigationCard: some View {
        VStack(alignment: .leading, spacing: 9) {
            HStack(spacing: 10) {
                Image(systemName: currentManeuver?.type.symbolName ?? "arrow.up").font(.title2.weight(.bold)).foregroundStyle(.blue)
                VStack(alignment: .leading, spacing: 1) {
                    Text(currentManeuver?.type.label ?? "Follow the route").font(.headline).lineLimit(1)
                    Text(currentManeuver?.streetName.isEmpty == false ? currentManeuver!.streetName : "Route guidance")
                        .font(.subheadline).foregroundStyle(.secondary).lineLimit(1)
                }
                Spacer(minLength: 4)
                VStack(alignment: .trailing, spacing: 1) { Text(distanceText(maneuverDistance)).font(.title3.bold()); Text("to turn").font(.caption).foregroundStyle(.secondary) }
            }
            HStack(spacing: 18) { metric("Remaining", distanceText(bluetooth.status?.remainingM ?? UInt32(route.distanceM))); metric("Status", runState.label); Spacer(minLength: 0) }
            HStack(spacing: 9) {
                Button {
                    switch runState { case .running: bluetooth.pauseNavigation(); case .paused: bluetooth.resumeNavigation(); case .arrived: bluetooth.restartNavigation(); case .notStarted, .stopped: bluetooth.startNavigation() }
                } label: { Label(primaryTitle, systemImage: primarySymbol).frame(maxWidth: .infinity, minHeight: 46) }
                    .buttonStyle(.borderedProminent).contentShape(Rectangle())
                    .disabled(!bluetooth.isReadyForTransfer || !bluetooth.routeIsActive(route) || bluetooth.pendingNavigationCommand != nil)
                Menu {
                    Button("Restart from beginning", systemImage: "backward.end") { bluetooth.restartNavigation() }
                        .disabled(!bluetooth.isReadyForTransfer || bluetooth.pendingNavigationCommand != nil)
                    Button("Route overview", systemImage: "map") { fitRoute() }
                    Button("Heading-up map", systemImage: "location.north") { headingUp.toggle(); recenter() }
                    Divider()
                    Button("End ride", systemImage: "stop.fill", role: .destructive) { bluetooth.stopNavigation(); dismiss() }
                } label: {
                    Image(systemName: "ellipsis")
                        .frame(width: 46, height: 46)
                        .contentShape(Rectangle())
                }
                    .buttonStyle(.bordered).contentShape(Rectangle())
            }
            if bluetooth.pendingNavigationCommand != nil { ProgressView("Waiting for ESP32…").font(.caption) }
            if !bluetooth.routeIsActive(route) {
                Text("This route is not active on the ESP32 yet. Return to Home, tap Send to BikeOS, and wait for activation.")
                    .font(.caption).foregroundStyle(.orange).lineLimit(2)
            }
            Text(bluetooth.lastAck).font(.caption).foregroundStyle(.secondary).lineLimit(1)
        }
        .padding(.horizontal, 16).padding(.top, 12).padding(.bottom, 10).background(.regularMaterial)
        .frame(maxWidth: .infinity, alignment: .leading)
        .transaction { transaction in transaction.animation = nil }
    }

    private func metric(_ title: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 1) { Text(title.uppercased()).font(.caption2.weight(.semibold)).foregroundStyle(.secondary); Text(value).font(.subheadline.weight(.semibold)).lineLimit(1) }
    }
    private func distanceText(_ metres: UInt32) -> String { metres >= 1000 ? String(format: "%.1f km", Double(metres) / 1000) : "\(metres) m" }
    private func recenter() { followsRider = true; followRiderIfNeeded() }
    private func fitRoute() { suppressCameraChange = true; cameraPosition = .region(route.region); DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { suppressCameraChange = false } }
    private func followRiderIfNeeded() {
        guard followsRider, let coordinate = bluetooth.liveCoordinate else { return }
        suppressCameraChange = true
        cameraPosition = .camera(MapCamera(centerCoordinate: coordinate,
                                           distance: 850,
                                           heading: headingUp ? (displayHeading ?? 0) : 0,
                                           pitch: 0))
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { suppressCameraChange = false }
    }
}

private extension BikeOSRoutePlan {
    var region: MKCoordinateRegion {
        guard !points.isEmpty else { return MKCoordinateRegion() }
        let lats = points.map(\.latitude), lons = points.map(\.longitude)
        return MKCoordinateRegion(center: CLLocationCoordinate2D(latitude: (lats.min()! + lats.max()!) / 2, longitude: (lons.min()! + lons.max()!) / 2), span: MKCoordinateSpan(latitudeDelta: max(0.005, (lats.max()! - lats.min()!) * 1.35), longitudeDelta: max(0.005, (lons.max()! - lons.min()!) * 1.35)))
    }

    func bearing(at index: Int) -> CLLocationDirection? {
        guard points.count >= 2 else { return nil }
        let start = points[min(max(0, index), points.count - 2)]
        let end = points[min(max(1, index + 1), points.count - 1)]
        let lat1 = start.latitude * .pi / 180
        let lat2 = end.latitude * .pi / 180
        let deltaLon = (end.longitude - start.longitude) * .pi / 180
        let y = sin(deltaLon) * cos(lat2)
        let x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(deltaLon)
        let bearing = atan2(y, x) * 180 / .pi
        return bearing >= 0 ? bearing : bearing + 360
    }
}

private extension BikeOSGpsSource {
    var label: String { switch self { case .simulated: "simulated"; case .uart: "NEO-6M"; case .iPhoneFallback: "iPhone"; case .none: "none" } }
}

private extension BikeOSNavigationState {
    var label: String {
        switch self { case .navigating: "Navigating"; case .destinationReached: "Arrived"; case .phoneDisconnected: "Phone disconnected"; case .offRoute: "Off route"; case .rerouting: "Rerouting"; case .gpsLost: "GPS lost"; case .waitingForGPS: "Waiting for GPS"; case .noRoute: "No route"; case .error: "Error" }
    }
}

private extension BikeOSRunState {
    var label: String { switch self { case .notStarted: "Not started"; case .running: "Running"; case .paused: "Paused"; case .stopped: "Stopped"; case .arrived: "Arrived" } }
}
