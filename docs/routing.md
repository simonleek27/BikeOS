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

The app encodes the selected geometry and maneuvers as BRTE v1, including the
route ID and CRC. Coordinates are signed 16-bit offsets in 1e-6-degree units
from the first point. Routes outside that representable window are rejected
with a user-facing error rather than being clipped or silently wrapped.

The Amsterdam demo sends its checked-in BMAP. A custom route sends a
route-only bundle (`map_size = 0`); the firmware then keeps the map stored in
its flash (currently the bundled Amsterdam demo area, about 1 km² around
Museumplein), so the route is drawn over the same streets, water and parks.
Outside the stored area the route is drawn on its own. A tiled map of all of
Amsterdam in a dedicated flash partition is the next map-data milestone.

After the transfer commit is acknowledged, the ESP32 status stream is the
source of truth for the active route, position, maneuvers, remaining distance,
and run state.
