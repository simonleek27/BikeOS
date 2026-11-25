# Map and route data

BikeOS does not download raster tiles at runtime. The phone calculates a route
and sends the route geometry and maneuvers; the ESP32 supplies the surrounding
road context from its offline city map.

## Amsterdam city map (BTIL)

The current integration stores `data/amsterdam.btil` in a dedicated 12 MB
`map` flash partition. It contains 1,115 500 m tiles covering Amsterdam, with
roads plus selected parks and water areas. At runtime the ESP32 keeps only the
3×3 tiles around the rider decoded in PSRAM. As the rider crosses a tile
boundary, newly needed tiles are CRC-checked and decoded; the map is therefore
rider-centered rather than limited to the planned route.

The phone sends route-only bundles for custom routes. If the city partition is
blank or corrupt, firmware falls back to the small embedded demo map. A custom
route outside the BTIL coverage is rejected instead of showing misleading
surrounding streets.

Flash the app and partition table with `idf.py flash`, then flash the city map
separately:

```sh
idf.py -p /dev/cu.usbmodemXXXX flash
idf.py -p /dev/cu.usbmodemXXXX map-flash
```

Regenerate the city asset from a Geofabrik extract:

```sh
curl -LO https://download.geofabrik.de/europe/netherlands/noord-holland-latest.osm.pbf
python3 tools/build_city_tiles.py --pbf noord-holland-latest.osm.pbf
```

## Route format

`BMAP` v4 stores map features with CRCs and quantized coordinates. `BRTE` v1
uses signed 16-bit offsets from a route origin; `BRTE` v2 uses absolute signed
32-bit E7 coordinates and is selected automatically for routes that do not fit
the v1 coordinate window. This allows longer routes without silent overflow.

The firmware and iPhone share the same route decoder and validate magic,
version, counts, lengths, coordinate ranges, and CRCs before activation.

The older `data/amsterdam_map.pack` implementation remains in the host test
suite for compatibility regression coverage, but the integrated firmware uses
the BTIL partition because it is larger, independently updateable, and includes
the complete city window.

OpenStreetMap-derived data must retain the applicable attribution and ODbL
obligations: [OpenStreetMap copyright and license](https://www.openstreetmap.org/copyright).
