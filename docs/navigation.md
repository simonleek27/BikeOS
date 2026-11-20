# Navigation core

The shared C++ core consumes a validated route and a stream of `GpsFix` values.
Coordinates are signed 1e-7-degree integers; local metric work uses an
equirectangular projection centered on the current route area.

For each accepted fix, the engine projects the position onto route segments,
records the closest point and cross-track distance, advances route progress,
and finds the next maneuver from its cumulative-distance table. It declares an
arrival inside the destination threshold.

Off-route behavior is intentionally stateful: a cross-track distance over 30 m
must persist for five updates before a reroute request is emitted. Further
requests are throttled. While no fix is available the state becomes `GPS_LOST`;
while a replacement route is pending it remains explicit rather than silently
pretending the old route is valid.

The deterministic simulator follows the same route geometry and can inject
noise, lateral deviations, GPS loss, jumps, and stops. The desktop app and the
current firmware demo use it, so state transitions can be reproduced before a
GPS module is connected.

## Authoritative run state

Navigation progress is gated by `NavigationRunState` and mirrored in the 36-byte
BLE status extension. A newly installed route is `not_started`; only an
acknowledged `start_navigation`/`resume_navigation` command advances the
simulator. `pause_navigation` freezes the exact matched point, remaining
distance, and rider marker while reporting speed `0`. `restart_navigation`
resets the route to its first point and resumes; arrival is terminal until a
restart. The iPhone renders these states rather than inferring pause from the
older navigation-state enum.

Custom iPhone routes are encoded as BRTE v1. The ESP32 accepts a route-only
bundle with an empty BMAP and builds a bounding box from the route geometry, so
the route remains usable outside the bundled Amsterdam map. No unrelated roads
are rendered for those routes.
