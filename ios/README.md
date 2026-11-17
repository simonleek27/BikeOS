# BikeOS iPhone app

Open `BikeOS.xcodeproj` in Xcode 26. The app runs in the simulator with the Amsterdam demo screen and uses CoreBluetooth on a real iPhone. Tap **Connect**, select the advertised `BikeOS` peripheral, then subscribe/read status through the app. The app’s UUIDs and frame CRC are implemented in `BikeOSProtocol.swift` and match `protocol/include/bikeos/protocol.hpp` byte-for-byte.

The ESP32 is currently an open development peripheral (no pairing required). Before production, enable authenticated BLE characteristics and bonding. GraphHopper is an adapter only; keep the key in an untracked xcconfig. The checked-in route assets are the deterministic first demo; arbitrary OSM extraction still requires the map preparation tool and a chosen data source.
