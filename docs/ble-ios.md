# BLE + iPhone bring-up

The ESP32-S3 firmware advertises as `BikeOS` on a changing `/dev/cu.usbmodem*` port. The service and characteristic UUIDs are listed in `protocol/include/bikeos/protocol.hpp` and mirrored in `ios/BikeOS/Services/BikeOSProtocol.swift`.

## Physical first test with nRF Connect

1. Leave the ESP32 connected by USB and flash with `idf.py -p /dev/cu.usbmodem101 flash`.
2. Install nRF Connect on the iPhone and allow Bluetooth access.
3. Scan, select `BikeOS`, connect, and discover the custom service.
4. Enable notifications on the acknowledgement and status characteristics.
5. Read Device Info; it should contain `protocol=1`.
6. Write a status-request frame to Control (`42 4B 01 01` plus the 16-byte zero header fields and CRC payload for an empty payload). Confirm an acknowledgement notification.

The real iPhone connection has been verified once: the app discovered `BikeOS`, connected, and the ESP32 log recorded a BLE connection and status notifications. Firmware 0.5.0 and the matching app perform the full ACK-paced bundle transfer, verify the activated route ID, and expose explicit navigation controls. Every write records the characteristic properties and negotiated response/no-response limits. Frames that do not fit are wrapped in the version-1 BLE fragment envelope; the ESP32 rejects malformed, duplicate, out-of-order, or oversized fragments and only decodes a complete original frame. This removes the CoreBluetooth “value's length is invalid” failure on low-MTU links.

The status payload extension (36 bytes) now carries an authoritative run state:
`not_started`, `running`, `paused`, `stopped`, or `arrived`. The phone changes its
Play/Pause/Resume/Restart button only after the ESP32 acknowledgement and status
confirmation. While paused, the simulator stops advancing, reports speed 0, and
continues sending status notifications.

Custom routes are sent as route-only bundles (`map_size = 0`) with a synthesized
route bounding box. The route line and rider remain valid outside the bundled
Amsterdam BMAP area; surrounding offline roads are intentionally omitted rather
than shown incorrectly. The checked-in Amsterdam demo continues to send its BMAP
for full offline road context.

## App

Open `ios/BikeOS.xcodeproj`, choose the BikeOS scheme, and run on a real iPhone for CoreBluetooth. The home screen loads and validates `amsterdam_demo.brte`, draws the full route and maneuvers, and shows live ESP32 status. Tap **Connect**, then **Send demo route**. The app transfers the checked-in `amsterdam_demo.bmap` + `amsterdam_demo.brte` assets in protocol-sized chunks (up to 476-byte payloads); the BLE write layer fragments each frame to the negotiated ATT limit, paces each chunk from the ESP32 ACK, and only reports activation after the commit ACK and matching active route ID.

After activation, tap **Start**. The deterministic simulator runs indoors at
4.5 m/s. **Pause**, **Resume**, **Restart**, **Test off-route**, and **End ride**
send explicit control frames to the ESP32; status notifications remain the
authoritative source for the map marker and navigation state.

BLE is open development mode today. Before production, enable
encrypted/authenticated characteristics and bonding; do not treat the current
open mode as a security boundary.
