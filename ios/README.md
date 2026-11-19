# BikeOS iPhone app

Open `BikeOS.xcodeproj` in Xcode 26. The app runs in the simulator with the Amsterdam demo screen and uses CoreBluetooth on a real iPhone. On a real phone, leave the ESP32 connected to USB, tap **Connect**, select the advertised `BikeOS` peripheral, then tap **Send demo route**. Once the app reports the matching route ID as activated, use **Start**, **Pause**, **Resume**, **Restart**, **Test off-route**, and **End ride** to control the indoor simulator. The map follows the authoritative ESP32 status stream; it is not using the phone's location for this demo.

The app’s UUIDs, frame CRC, route decoder, and transfer pacing are implemented
in `BikeOSProtocol.swift` and `BikeOSBluetooth.swift`, matching
`protocol/include/bikeos/protocol.hpp` byte-for-byte. The transfer uses the
bundled `amsterdam_demo.bmap` and `amsterdam_demo.brte`, validates every ACK,
and verifies the final active route ID before enabling the live ride screen.

The ESP32 is currently an open development peripheral (no pairing required). Before production, enable authenticated BLE characteristics and bonding. GraphHopper is an adapter only; keep the key in an untracked xcconfig. The checked-in route assets are the deterministic first demo; arbitrary OSM extraction still requires the map preparation tool and a chosen data source.
