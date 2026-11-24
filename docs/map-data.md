# Map and route data

BikeOS does not download raster tiles at runtime. `tools/prepare_amsterdam_pack.py`
makes a bounded Overpass query for OpenStreetMap highway geometry, splits it
into small BMAP tiles, and `tools/build_map_pack.py` stores those tiles in one
CRC-protected flash-resident pack. The ESP32 keeps the pack in flash but decodes
only tiles intersecting a square around the current GPS position. As the rider
moves, the working road set is replaced; it is not limited to the planned route.

- `BMAP` version 3/4: origin, bounds, road classes, stable OSM way IDs, a
  deduplicated UTF-8 street-name table, and coordinates quantized to 1e-6
  degrees as signed 16-bit offsets. Versions 1 and 2 remain decode-compatible
  for migration;
- `BRTE` v1/v2: route id, coordinates, and maneuver records
  with bounded street-name strings.

The checked-in offline pack covers the Amsterdam development region in 136
tiles (~2 MB). Maneuver names come
from the outgoing OSM way at each real intersection; the destination maneuver
uses the final route way rather than a placeholder label. Both host and
firmware use the same decoders and reject bad magic, versions, lengths,
coordinate ranges, counts, or CRCs.

Regenerate from the network:

```sh
python3 tools/prepare_amsterdam_demo.py --output-dir data
python3 tools/prepare_amsterdam_pack.py --output data/amsterdam_map.pack
```

Public Overpass instances are appropriate for this small development extract,
not production bulk downloads. A production app should use a suitable hosted
provider or prebuilt regional extracts. Derived data must retain OpenStreetMap
attribution and ODbL obligations: [OpenStreetMap copyright and
license](https://www.openstreetmap.org/copyright).
