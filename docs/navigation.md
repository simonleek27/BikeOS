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
