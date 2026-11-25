# BikeOS iPhone app

The app stores the active route and latest ESP32 checkpoint locally on the
iPhone. After an ESP32 power cycle, reconnect and wait for **Ready**; BikeOS
restores the route and position paused, then **Resume** continues from that
offset. **Restart from beginning** intentionally resets to the start. The
navigation arrow uses ESP32 heading when available, with iPhone GPS/compass and
route bearing as fallbacks.

Open `BikeOS.xcodeproj` in Xcode 26. The app runs in the simulator with the Amsterdam demo planner and uses CoreBluetooth on a real iPhone. On a real phone, leave the ESP32 connected to USB, tap **Connect**, select the advertised `BikeOS` peripheral, then tap **Send to BikeOS**. Once the app reports the matching route ID as activated, open navigation and use **Start**, **Pause**, **Resume**, **Restart**, and **End ride** to control the indoor simulator. The map follows the authoritative ESP32 status stream; it is not using the phone's location for ride progress.

## Create a route

On Home, search for a destination with MapKit, choose **Current location** or
search for a manual start, and tap **Calculate bike route**. BikeOS calls
GraphHopper's `bike` profile with full geometry and instructions, draws the
preview, and shows distance, duration, and maneuver street names. Configure the
key without committing it:

```sh
cp ios/GraphHopper.example.xcconfig ios/GraphHopper.xcconfig
# edit the copied file, then use it as the Xcode scheme configuration
```

The endpoint line must remain in the provided escaped form (`https:/$()/...`);
Xcode's `.xcconfig` parser treats a literal `//` as a comment and would otherwise
truncate the URL to `https:`. The app now reports missing keys, invalid endpoints,
HTTP errors, and no-route responses directly in the planner. If **Current
location** has not produced a fix yet, choose **Choose start** and search for an
Amsterdam starting point instead.

Alternatively set `GRAPHHOPPER_API_KEY` as a scheme environment variable. If no
key is available, the Amsterdam demo remains usable. Routing failures are shown
in the app; it never substitutes a straight-line or fictional route.

The current ESP32 BRTE v1 route format uses signed 16-bit offsets from the first
point. BikeOS rejects a route whose extent exceeds that representable window,
instead of silently overflowing coordinates. A valid custom route is transferred
as route-only data, so it works beyond the bundled Amsterdam map without
pretending the old offline roads cover it. Broader offline BMAP coverage is a
future map-data milestone.

The compact home/planner layout was built and captured on an iPhone 17 Pro
simulator: [home screen](../docs/screenshots/iphone-home.png).

The app’s UUIDs, frame CRC, route decoder, and transfer pacing are implemented
in `BikeOSProtocol.swift` and `BikeOSBluetooth.swift`, matching
`protocol/include/bikeos/protocol.hpp` byte-for-byte. The transfer uses the
bundled `amsterdam_demo.bmap` and `amsterdam_demo.brte`, validates every ACK,
and verifies the final active route ID before enabling the live ride screen.

The ESP32 is currently an open development peripheral (no pairing required). Before production, enable authenticated BLE characteristics and bonding. GraphHopper is an adapter only; keep the key in an untracked xcconfig. The checked-in route assets are the deterministic first demo; arbitrary OSM extraction still requires the map preparation tool and a chosen data source.
