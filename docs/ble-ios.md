# BLE + iPhone bring-up

The ESP32-S3 firmware advertises as `BikeOS` on a changing `/dev/cu.usbmodem*` port. The service and characteristic UUIDs are listed in `protocol/include/bikeos/protocol.hpp` and mirrored in `ios/BikeOS/Services/BikeOSProtocol.swift`.

## Physical first test with nRF Connect

1. Leave the ESP32 connected by USB and flash with `idf.py -p /dev/cu.usbmodem101 flash`.
2. Install nRF Connect on the iPhone and allow Bluetooth access.
3. Scan, select `BikeOS`, connect, and discover the custom service.
4. Enable notifications on the acknowledgement and status characteristics.
5. Read Device Info; it should contain `protocol=1`.
6. Write a status-request frame to Control (`42 4B 01 01` plus the 16-byte zero header fields and CRC payload for an empty payload). Confirm an acknowledgement notification.

The real iPhone connection has been verified once: the app discovered `BikeOS`, connected, and the ESP32 log recorded a BLE connection and status notifications. The current app performs the full ACK-paced bundle transfer, verifies the activated route ID, and exposes explicit navigation controls. The control characteristic accepts both BLE write modes so larger metadata frames can use the negotiated ATT MTU without triggering CoreBluetooth's invalid-length error. Repeat the route transfer with the board serial monitor open to capture the final commit ACK and activated route status.

## App

Open `ios/BikeOS.xcodeproj`, choose the BikeOS scheme, and run on a real iPhone for CoreBluetooth. The home screen loads and validates `amsterdam_demo.brte`, draws the full route and maneuvers, and shows live ESP32 status. Tap **Connect**, then **Send demo route**. The app transfers the checked-in `amsterdam_demo.bmap` + `amsterdam_demo.brte` assets in 150-byte chunks, pacing each chunk from the ESP32 ACK, and only reports activation after the commit ACK and matching active route ID.

After activation, tap **Start**. The deterministic simulator runs indoors at
4.5 m/s. **Pause**, **Resume**, **Restart**, **Test off-route**, and **End ride**
send explicit control frames to the ESP32; status notifications remain the
authoritative source for the map marker and navigation state.

BLE is open development mode today. Before production, enable
encrypted/authenticated characteristics and bonding; do not treat the current
open mode as a security boundary.
