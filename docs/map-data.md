# Map and route data

BikeOS does not download raster tiles. `tools/prepare_amsterdam_demo.py` makes a
bounded Overpass query for OpenStreetMap highway geometry, simplifies the
polylines, computes a connected demo route, and writes two CRC-protected binary
assets:

- `BMAP` version 3: origin, bounds, road classes, stable OSM way IDs, a
  deduplicated UTF-8 street-name table, and coordinates quantized to 1e-6
  degrees as signed 16-bit offsets. Versions 1 and 2 remain decode-compatible
  for migration;
- `BRTE`: route id, full 1e-7-degree route coordinates, and maneuver records
  with bounded street-name strings.

The checked-in Amsterdam sample contains 1,017 named road polylines / 2,936
points and an 84-point, 1.41 km route with five maneuvers. Maneuver names come
from the outgoing OSM way at each real intersection; the destination maneuver
uses the final route way rather than a placeholder label. Both host and
firmware use the same decoders and reject bad magic, versions, lengths,
coordinate ranges, counts, or CRCs.

Regenerate from the network:

```sh
python3 tools/prepare_amsterdam_demo.py --output-dir data
```

Public Overpass instances are appropriate for this small development extract,
not production bulk downloads. A production app should use a suitable hosted
provider or prebuilt regional extracts. Derived data must retain OpenStreetMap
attribution and ODbL obligations: [OpenStreetMap copyright and
license](https://www.openstreetmap.org/copyright).
