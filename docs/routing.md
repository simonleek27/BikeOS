# iPhone bicycle routing

BikeOS uses MapKit for destination and optional manual-start search, then calls
GraphHopper's `bike` profile for road geometry, street names, distances,
duration, and turn instructions. The app draws the returned route in a preview
before anything is sent to the ESP32.

## Configure GraphHopper

Keep the API key local and untracked:

```sh
cp ios/GraphHopper.example.xcconfig ios/GraphHopper.xcconfig
# edit the copied key
```

Use that file as the Xcode build configuration, or set
`GRAPHHOPPER_API_KEY` in the BikeOS Run scheme's environment variables. The
base URL defaults to `https://graphhopper.com/api/1/route` and can be overridden
with `GRAPHHOPPER_BASE_URL`. If no key is available, the bundled Amsterdam demo
continues to work and the app shows a routing error instead of fabricating a
straight-line route.

## ESP32 route format

The app encodes the selected geometry and maneuvers as BRTE. It uses v1 when
the route fits the original signed-16-bit 1e-6-degree corridor and automatically
uses v2 when it does not. BRTE v2 stores absolute signed 32-bit E7 coordinates,
retains the same maneuver records and CRC, and is accepted by both Swift and
firmware decoders. Encoded routes remain limited to 512 KiB.

The Amsterdam demo can send its checked-in BMAP. A custom route sends a
route-only bundle (`map_size = 0`); the ESP32 validates every point against the
offline Amsterdam BTIL city archive, draws it over those streets, and refreshes
the 3×3 working tile window around the actual GPS position. Roads therefore
remain visible after the rider leaves the route. Routes outside BTIL coverage
are rejected explicitly.

After the transfer commit is acknowledged, the ESP32 status stream is the
source of truth for the active route, position, maneuvers, remaining distance,
and run state.
