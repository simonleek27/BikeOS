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

## Tiled city map (BTIL)

The device stores all of Amsterdam in a 12 MB `map` flash partition and the
phone only sends routes. `tools/build_city_tiles.py` fetches OSM roads (the
classes of `prepare_amsterdam_demo.py`) and water and park areas (as
`add_map_areas.py`, including the 2,500 m² park cutoff, applied to the whole
park before it is cut) for the municipality, cuts them into square tiles, and
writes `data/amsterdam.btil`. It reads either a Geofabrik extract (`--pbf
noord-holland-latest.osm.pbf`, about a minute with `pip install osmium`) or
the public Overpass API in ~4 km chunks, which takes hours and often fails on
504s; use the extract.

```sh
curl -LO https://download.geofabrik.de/europe/netherlands/noord-holland-latest.osm.pbf
python3 tools/build_city_tiles.py --pbf noord-holland-latest.osm.pbf \
    --save-highways host/virtual_screen/.cache/osm_highways_amsterdam.json
```

Built from the 2026-11-23 extract: 1,115 tiles of 500 m in a 52 x 35 grid,
3.86 MB in all (tiles 81 B to 15.6 KB, median 3.4 KB), so the 12 MB partition
has room for growth or a second city. The layout:

```
header (40 B)  "BTIL", version 1, flags 0,
               grid origin (south-west corner, E7), tile height and width (E7),
               columns, rows, tile count, total blob size,
               CRC-32 of header bytes 0..35 + the whole index
index          per tile, sorted by (row, column): column u16, row u16,
               offset u32, size u32, CRC-32 u32 of the tile bytes
tiles          one complete BMAP v4 map per tile
```

A tile's bounds follow from its grid cell, so the index stores the cell rather
than four coordinates. Each tile is an ordinary BMAP v4 map whose origin is
the tile centre and whose bounds are the cell, so the 16-bit coordinate offsets
(±3.6 km) only have to reach 250 m and no longer limit the map's extent. Roads
are cut at tile edges with Liang-Barsky clipping and areas with
Sutherland-Hodgman; the cut point on a shared edge is computed the same way in
both tiles, and tile sizes and the grid origin are multiples of 20 E7 so every
tile centre lies on the 1e-6° quantization grid and cut points encode
identically on both sides. Tiles that do not touch the municipality, and empty
tiles, are left out.

**Why 500 m tiles.** At the cruise zoom (1.6 m/px) the farthest screen corner
is about 400 m from the rider. The device keeps the 3x3 tiles around the rider
and follows the rider into a new tile only after 60 m of hysteresis, so the map
always reaches at least 500 − 60 = 440 m in every direction: 500 m is the
smallest round size where a 3x3 window always fills the screen. Larger tiles
would hold four times the features per megabyte of RAM for 1 km tiles and take
longer to decode on each crossing; smaller ones would need a 5x5 window. At
16 km/h the rider crosses a tile edge about every two minutes, and each
crossing decodes at most three new tiles (five at a corner).

**On the device.** `NavigationRuntime` memory-maps the partition
(`esp_partition_mmap`), checks the header and index CRC at boot, and in the
1 Hz navigation step lets `TileWindow` (core/src/map_tiles.cpp) load the tiles
around the rider. Kept tiles are moved, not decoded again; new tiles are
CRC-checked and decoded with `decode_map`, and the merged `MapDataset` gets a
new `revision` so the renderer rebuilds its cached bounding circles. Decoded
features are allocated in PSRAM. The busiest 3x3 window (around Centraal)
holds about 3,900 road pieces and takes roughly 315 KiB of heap, all in PSRAM;
decoding a full window takes 0.6 ms on the host at worst, so a crossing on the
ESP32-S3 should cost a few milliseconds (not yet measured on the board).
With a blank or corrupt partition the
firmware uses the embedded demo map. Flashing is described in
`firmware/README.md`.
