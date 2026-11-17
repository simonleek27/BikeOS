# BLE + iPhone bring-up

The ESP32-S3 firmware now advertises as `BikeOS` on `/dev/cu.usbmodem101` (the port can change). The service and characteristic UUIDs are listed in `protocol/include/bikeos/protocol.hpp` and mirrored in `ios/BikeOS/Services/BikeOSProtocol.swift`.

## Physical first test with nRF Connect

1. Leave the ESP32 connected by USB and flash with `idf.py -p /dev/cu.usbmodem101 flash`.
2. Install nRF Connect on the iPhone and allow Bluetooth access.
3. Scan, select `BikeOS`, connect, and discover the custom service.
4. Enable notifications on the acknowledgement and status characteristics.
5. Read Device Info; it should contain `protocol=1`.
6. Write a status-request frame to Control (`42 4B 01 01` plus the 16-byte zero header fields and CRC payload for an empty payload). Confirm an acknowledgement notification.

This is the step Simon must perform on the phone; local tools cannot verify a real iPhone radio connection. The firmware log is only proof that the board booted and advertised.

## App

Open `ios/BikeOS.xcodeproj`, choose the BikeOS scheme, and run on a simulator for the UI/demo route. CoreBluetooth is intentionally exercised only on a real iPhone. Tap Connect, then Send demo route; the app transfers the checked-in `amsterdam_demo.bmap` + `amsterdam_demo.brte` assets in 150-byte chunks and waits for the ESP32 ACK.

BLE is open development mode today. Before production, enable encrypted/authenticated characteristics and bonding; do not treat the current open mode as a security boundary.
