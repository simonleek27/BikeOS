#!/usr/bin/env python3
"""Build a tiled BikeOS city map (BTIL) for the ESP32 map partition.

Fetches OpenStreetMap roads, water and parks for the Amsterdam municipality
from Overpass in small chunks (one city-wide query times out), cuts them into
~500 m square tiles, and writes one blob: a CRC-protected tile index followed
by one BMAP v4 map per tile, each with its own origin.

    python3 tools/build_city_tiles.py                       # fetch, then write data/amsterdam.btil
    python3 tools/build_city_tiles.py --cache-dir /tmp/osm  # reuse saved chunk responses
    python3 tools/build_city_tiles.py --pbf noord-holland-latest.osm.pbf  # local extract, about a minute

Overpass is a shared public server and a city of chunks takes hours there. The
fast route is a Geofabrik extract (download.geofabrik.de/europe/netherlands/
noord-holland-latest.osm.pbf, ~190 MB) read with pyosmium (pip install osmium).

Road classes match tools/prepare_amsterdam_demo.py and areas (with the
2,500 m^2 park cutoff) match tools/add_map_areas.py. The format is described in
docs/map-data.md and decoded by core/src/map_tiles.cpp.
"""

from __future__ import annotations

import argparse
import binascii
import datetime as dt
import json
import math
import pathlib
import struct
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from prepare_amsterdam_demo import ROAD_CLASS, simplify  # noqa: E402
from add_map_areas import MIN_PARK_AREA_M2, PARK, WATER, classify, ring_area_m2, stitch  # noqa: E402

ENDPOINTS = (
    "https://overpass-api.de/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
    "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
)
USER_AGENT = "BikeOS/0.3 city-tiler (https://github.com/simonleek27/BikeOS)"
# Bounding box of the Amsterdam municipality (OSM relation 271110, admin_level 8).
AMSTERDAM_BBOX = (52.2779, 4.7287, 52.4311, 5.1077)
AREA_SELECTORS = (
    '["natural"="water"]', '["waterway"="riverbank"]', '["landuse"="basin"]',
    '["leisure"="park"]', '["leisure"="garden"]["access"!="private"]',
    '["landuse"~"^(grass|recreation_ground|village_green|meadow|forest)$"]', '["natural"="wood"]',
)
METRES_PER_DEGREE = 6_371_008.8 * math.pi / 180.0
QUANTIZATION_E7 = 10
ROAD_TOLERANCE_M = 1.5   # as prepare_amsterdam_demo.py
AREA_TOLERANCE_M = 1.0   # as add_map_areas.py


# ---------------------------------------------------------------- fetching

def overpass(query: str) -> dict:
    last_error = ""
    for attempt in range(8):
        endpoint = ENDPOINTS[attempt % len(ENDPOINTS)]
        result = subprocess.run(
            ["curl", "--fail", "--silent", "--show-error", "--max-time", "400",
             "-A", USER_AGENT, "--data-urlencode", f"data={query}", endpoint],
            check=False, capture_output=True,
        )
        if result.returncode == 0:
            try:
                data = json.loads(result.stdout)
                if "remark" in data and "runtime error" in data["remark"]:
                    raise ValueError(data["remark"])
                return data
            except ValueError as error:
                last_error = str(error)
        else:
            last_error = result.stderr.decode().strip()
        wait = min(120, 15 * (attempt + 1))
        print(f"  {endpoint}: {last_error[:120]}; retrying in {wait}s", file=sys.stderr)
        time.sleep(wait)
    raise RuntimeError(f"Overpass request failed: {last_error}")


def chunk_boxes(bbox, chunk_deg: float):
    south, west, north, east = bbox
    lat_steps = max(1, math.ceil((north - south) / chunk_deg))
    lon_steps = max(1, math.ceil((east - west) / (chunk_deg * 1.6)))
    for i in range(lat_steps):
        for j in range(lon_steps):
            s = south + (north - south) * i / lat_steps
            n = south + (north - south) * (i + 1) / lat_steps
            w = west + (east - west) * j / lon_steps
            e = west + (east - west) * (j + 1) / lon_steps
            yield f"{i:02d}-{j:02d}", (s, w, n, e)


def fetch_city(bbox, chunk_deg: float, cache_dir: pathlib.Path | None) -> tuple[list[dict], str | None]:
    """All highway ways and area ways/relations touching the bbox, deduplicated."""
    elements: dict[tuple[str, int], dict] = {}
    timestamp = None
    chunks = list(chunk_boxes(bbox, chunk_deg))
    for number, (name, (s, w, n, e)) in enumerate(chunks, 1):
        box = f"({s:.6f},{w:.6f},{n:.6f},{e:.6f})"
        cached = cache_dir / f"chunk-{name}.json" if cache_dir else None
        if cached and cached.exists():
            data = json.loads(cached.read_text())
        else:
            areas = "".join(f"way{sel}{box};relation{sel}{box};" for sel in AREA_SELECTORS)
            query = f"[out:json][timeout:300];(way[highway]{box};{areas});out geom;"
            print(f"fetching chunk {number}/{len(chunks)} {box}", file=sys.stderr)
            data = overpass(query)
            if cached:
                cached.parent.mkdir(parents=True, exist_ok=True)
                cached.write_text(json.dumps(data, separators=(",", ":")))
            time.sleep(2)
        timestamp = timestamp or data.get("osm3s", {}).get("timestamp_osm_base")
        for element in data.get("elements", []):
            elements[(element["type"], element["id"])] = element
    return list(elements.values()), timestamp


def fetch_boundary(cache_dir: pathlib.Path | None) -> list[list[tuple[float, float]]]:
    cached = cache_dir / "boundary.json" if cache_dir else None
    if cached and cached.exists():
        data = json.loads(cached.read_text())
    else:
        data = overpass('[out:json][timeout:120];relation["boundary"="administrative"]'
                        '["admin_level"="8"]["name"="Amsterdam"];out geom;')
        if cached:
            cached.parent.mkdir(parents=True, exist_ok=True)
            cached.write_text(json.dumps(data, separators=(",", ":")))
    relation = data["elements"][0]
    paths = [[(p["lat"], p["lon"]) for p in m.get("geometry", [])]
             for m in relation.get("members", []) if m.get("type") == "way" and m.get("role") == "outer"]
    return [ring for ring in stitch(paths) if len(ring) >= 4]


# ---------------------------------------------------------------- local extract

def area_selected(tags) -> bool:
    """The same filter as AREA_SELECTORS, for tags read from a PBF extract."""
    natural, landuse, leisure = tags.get("natural"), tags.get("landuse"), tags.get("leisure")
    return (natural in ("water", "wood") or tags.get("waterway") == "riverbank"
            or landuse in ("basin", "grass", "recreation_ground", "village_green", "meadow", "forest")
            or leisure == "park" or (leisure == "garden" and tags.get("access") != "private"))


def read_pbf(path: pathlib.Path, bbox):
    """Overpass-shaped elements (ways and relations with geometry) touching `bbox`, plus the
    municipality boundary paths, read from an OSM PBF extract in two passes."""
    import osmium  # pip install osmium

    south, west, north, east = bbox

    def touches(points) -> bool:
        if not points:
            return False
        lats = [p["lat"] for p in points]
        lons = [p["lon"] for p in points]
        return min(lats) <= north and max(lats) >= south and min(lons) <= east and max(lons) >= west

    relations, boundary, member_ways = [], None, set()
    relation_filter = osmium.filter.EntityFilter(osmium.osm.RELATION)
    for relation in osmium.FileProcessor(str(path)).with_filter(relation_filter):
        tags = dict(relation.tags)
        is_boundary = (tags.get("boundary") == "administrative" and tags.get("admin_level") == "8"
                       and tags.get("name") == "Amsterdam")
        if not is_boundary and not (tags.get("type") == "multipolygon" and area_selected(tags)):
            continue
        members = [(m.ref, m.role) for m in relation.members if m.type == "w"]
        member_ways.update(ref for ref, _ in members)
        entry = {"type": "relation", "id": relation.id, "tags": tags, "members": members}
        if is_boundary:
            boundary = entry
        else:
            relations.append(entry)

    ways, geometry = [], {}
    way_filter = osmium.filter.EntityFilter(osmium.osm.WAY)
    for way in osmium.FileProcessor(str(path)).with_locations().with_filter(way_filter):
        tags = dict(way.tags)
        wanted = tags.get("highway") in ROAD_CLASS or area_selected(tags)
        if not wanted and way.id not in member_ways:
            continue
        points = [{"lat": n.location.lat, "lon": n.location.lon} for n in way.nodes if n.location.valid()]
        if way.id in member_ways:
            geometry[way.id] = points
        if wanted and touches(points):
            ways.append({"type": "way", "id": way.id, "tags": tags, "geometry": points})

    elements = list(ways)
    for relation in relations:
        members = [{"type": "way", "ref": ref, "role": role, "geometry": geometry.get(ref, [])}
                   for ref, role in relation["members"]]
        if any(touches(m["geometry"]) for m in members):
            elements.append({**relation, "members": members})
    boundary_paths = []
    if boundary:
        boundary_paths = [[(p["lat"], p["lon"]) for p in geometry.get(ref, [])]
                          for ref, role in boundary["members"] if role == "outer"]
    header = osmium.io.Reader(str(path), osmium.osm.osm_entity_bits.NOTHING)
    timestamp = header.header().get("osmosis_replication_timestamp") or None
    header.close()
    return elements, [ring for ring in stitch(boundary_paths) if len(ring) >= 4], timestamp


# ---------------------------------------------------------------- geometry

def e7(point: tuple[float, float]) -> tuple[int, int]:
    return round(point[0] * 1e7), round(point[1] * 1e7)


def point_in_rings(point, rings) -> bool:
    lat, lon = point
    inside = False
    for ring in rings:
        for (a_lat, a_lon), (b_lat, b_lon) in zip(ring, ring[1:] + ring[:1]):
            if (a_lat > lat) != (b_lat > lat):
                if lon < a_lon + (lat - a_lat) * (b_lon - a_lon) / (b_lat - a_lat):
                    inside = not inside
    return inside


def clip_polyline(points, south, west, north, east):
    """Liang-Barsky per segment in E7 units; same cut formula as core/src/map_tiles.cpp."""
    pieces, current = [], []

    def finish():
        nonlocal current
        if len(current) >= 2:
            pieces.append(current)
        current = []

    def push(point):
        if not current or current[-1] != point:
            current.append(point)

    for a, b in zip(points, points[1:]):
        d_lat, d_lon = b[0] - a[0], b[1] - a[1]
        t0, t1, edge0, edge1, outside = 0.0, 1.0, -1, -1, False
        for edge, (p, q) in enumerate(((-d_lat, a[0] - south), (d_lat, north - a[0]),
                                       (-d_lon, a[1] - west), (d_lon, east - a[1]))):
            if p == 0:
                if q < 0:
                    outside = True
                    break
                continue
            r = q / p
            if p < 0:
                if r > t1:
                    outside = True
                    break
                if r > t0:
                    t0, edge0 = r, edge
            else:
                if r < t0:
                    outside = True
                    break
                if r < t1:
                    t1, edge1 = r, edge
        if outside:
            finish()
            continue

        def cut(edge):
            value = (south, north, west, east)[edge]
            if edge < 2:
                t = (value - a[0]) / d_lat
                return (value, round(a[1] + d_lon * t))
            t = (value - a[1]) / d_lon
            return (round(a[0] + d_lat * t), value)

        start = a if edge0 < 0 else cut(edge0)
        end = b if edge1 < 0 else cut(edge1)
        if current and current[-1] != start:
            finish()
        push(start)
        push(end)
        if edge1 >= 0:
            finish()
    finish()
    return pieces


def clip_ring(ring, south, west, north, east):
    """Sutherland-Hodgman in E7 units; ring without the closing point."""
    points = [(float(p[0]), float(p[1])) for p in ring]
    for edge in range(4):
        if not points:
            break
        value = (south, north, west, east)[edge]

        def inside(p):
            return (p[0] >= value, p[0] <= value, p[1] >= value, p[1] <= value)[edge]

        def intersect(a, b):
            if edge < 2:
                t = (value - a[0]) / (b[0] - a[0])
                return (value, a[1] + (b[1] - a[1]) * t)
            t = (value - a[1]) / (b[1] - a[1])
            return (a[0] + (b[0] - a[0]) * t, value)

        output = []
        for i, current in enumerate(points):
            previous = points[i - 1]
            if inside(current):
                if not inside(previous):
                    output.append(intersect(previous, current))
                output.append(current)
            elif inside(previous):
                output.append(intersect(previous, current))
        points = output
    result = []
    for p in points:
        q = (round(p[0]), round(p[1]))
        if not result or result[-1] != q:
            result.append(q)
    while len(result) > 1 and result[0] == result[-1]:
        result.pop()
    return result


def twice_area(ring) -> float:
    return sum(a[1] * b[0] - b[1] * a[0] for a, b in zip(ring, ring[1:] + ring[:1]))


# ---------------------------------------------------------------- features

def parse_features(elements):
    roads, areas = [], []
    for element in elements:
        tags = element.get("tags", {})
        highway = tags.get("highway")
        if element["type"] == "way" and highway in ROAD_CLASS:
            geometry = [(p["lat"], p["lon"]) for p in element.get("geometry", []) if p]
            points = simplify(geometry, ROAD_TOLERANCE_M)
            if len(points) >= 2:
                name = tags.get("name", highway).encode("utf-8")[:96].decode("utf-8", errors="ignore")
                roads.append({"id": int(element["id"]) & 0xFFFFFFFF, "class": ROAD_CLASS[highway],
                              "highway": highway, "name": name, "points": [e7(p) for p in points]})
        area_class = classify(tags)
        if area_class is None:
            continue
        if element["type"] == "way":
            outer = [[(p["lat"], p["lon"]) for p in element.get("geometry", []) if p]]
            inner = []
        else:
            members = element.get("members", [])

            def geometry(role):
                return [[(p["lat"], p["lon"]) for p in m.get("geometry", []) if p]
                        for m in members if m.get("type") == "way" and m.get("role", "outer") == role]
            outer = stitch(geometry("outer") + geometry(""))
            inner = stitch(geometry("inner"))
        name = tags.get("name", "").encode("utf-8")[:96].decode("utf-8", errors="ignore")
        holes = []
        for hole in inner:
            simplified = simplify(hole + hole[:1], AREA_TOLERANCE_M)[:-1] if len(hole) >= 3 else []
            if len(simplified) >= 3:
                holes.append([e7(p) for p in simplified])
        for index, ring in enumerate(outer):
            if len(ring) < 3:
                continue
            simplified = simplify(ring + ring[:1], AREA_TOLERANCE_M)[:-1]
            if len(simplified) < 3:
                continue
            # The park cutoff applies to the whole park, not to its piece in one tile.
            if area_class == PARK and ring_area_m2(simplified) < MIN_PARK_AREA_M2:
                continue
            rings = [[e7(p) for p in simplified]]
            if index == 0:
                rings += holes
            areas.append({"class": area_class, "name": name, "rings": rings, "id": element["id"]})
    # Dedupe ways that are both a road and a member-less duplicate across chunks is done by id upstream.
    areas.sort(key=lambda area: area["class"] == WATER)  # parks first, water drawn over them
    return roads, areas


# ---------------------------------------------------------------- tiling

class Grid:
    def __init__(self, bbox, tile_m: float):
        south, west, north, east = bbox
        mid = math.radians((south + north) / 2)
        # Multiples of 20 E7 keep every tile centre on the 1e-6 degree
        # quantization grid, so points on a shared edge encode identically.
        self.tile_lat = round(tile_m / METRES_PER_DEGREE * 1e7 / 20) * 20
        self.tile_lon = round(tile_m / (METRES_PER_DEGREE * math.cos(mid)) * 1e7 / 20) * 20
        self.origin = (math.floor(south * 1e7 / 20) * 20, math.floor(west * 1e7 / 20) * 20)
        self.rows = math.ceil((north * 1e7 - self.origin[0]) / self.tile_lat)
        self.columns = math.ceil((east * 1e7 - self.origin[1]) / self.tile_lon)

    def cell(self, point):
        row = (point[0] - self.origin[0]) // self.tile_lat
        column = (point[1] - self.origin[1]) // self.tile_lon
        return min(max(column, 0), self.columns - 1), min(max(row, 0), self.rows - 1)

    def bounds(self, column, row):
        south = self.origin[0] + row * self.tile_lat
        west = self.origin[1] + column * self.tile_lon
        return south, west, south + self.tile_lat, west + self.tile_lon

    def centre(self, column, row):
        south, west, _, _ = self.bounds(column, row)
        return south + self.tile_lat // 2, west + self.tile_lon // 2

    def cells_for(self, points):
        lats = [p[0] for p in points]
        lons = [p[1] for p in points]
        c0, r0 = self.cell((min(lats), min(lons)))
        c1, r1 = self.cell((max(lats), max(lons)))
        for row in range(r0, r1 + 1):
            for column in range(c0, c1 + 1):
                yield column, row


def quantize(points, origin):
    out = []
    for lat, lon in points:
        q = (round((lat - origin[0]) / QUANTIZATION_E7), round((lon - origin[1]) / QUANTIZATION_E7))
        if not (-32768 <= q[0] <= 32767 and -32768 <= q[1] <= 32767):
            raise ValueError("point outside the int16 tile corridor")
        if not out or out[-1] != q:
            out.append(q)
    return out


def build_tiles(grid: Grid, roads, areas, keep_cell):
    tiles: dict[tuple[int, int], dict] = {}

    def tile(column, row):
        return tiles.setdefault((column, row), {"roads": [], "areas": []})

    for road in roads:
        for column, row in grid.cells_for(road["points"]):
            if not keep_cell(column, row):
                continue
            origin = grid.centre(column, row)
            for piece in clip_polyline(road["points"], *grid.bounds(column, row)):
                quantized = quantize(piece, origin)
                if len(quantized) >= 2:
                    tile(column, row)["roads"].append((road["id"], road["class"], road["name"], quantized))
    for area in areas:
        for column, row in grid.cells_for(area["rings"][0]):
            if not keep_cell(column, row):
                continue
            origin = grid.centre(column, row)
            rings = []
            for ring_index, ring in enumerate(area["rings"]):
                clipped = clip_ring(ring, *grid.bounds(column, row))
                quantized = quantize(clipped, origin) if len(clipped) >= 3 else []
                while len(quantized) > 1 and quantized[0] == quantized[-1]:
                    quantized.pop()
                if len(quantized) < 3 or twice_area(quantized) == 0:
                    if ring_index == 0:
                        break
                    continue
                rings.append(quantized)
                if len(rings) == 255:
                    break
            if rings:
                tile(column, row)["areas"].append((area["class"], area["name"], rings))
    return tiles


def encode_bmap(grid: Grid, column: int, row: int, content: dict) -> bytes:
    """BMAP v4, byte-compatible with encode_map() in core/src/map_data.cpp."""
    names: list[str] = []
    index: dict[str, int] = {}

    def intern(name):
        if name not in index:
            index[name] = len(names)
            names.append(name)
        return index[name]

    for _, _, name, _ in content["roads"]:
        intern(name)
    for _, name, _ in content["areas"]:
        intern(name)
    payload = bytearray(struct.pack("<H", len(names)))
    for name in names:
        encoded = name.encode("utf-8")
        payload += struct.pack("<B", len(encoded)) + encoded
    point_count = 0
    for feature_id, road_class, name, points in content["roads"]:
        payload += struct.pack("<BIHH", road_class, feature_id, len(points), index[name])
        for lat, lon in points:
            payload += struct.pack("<hh", lat, lon)
        point_count += len(points)
    payload += struct.pack("<H", len(content["areas"]))
    for area_class, name, rings in content["areas"]:
        payload += struct.pack("<BHB", area_class, index[name], len(rings))
        for ring in rings:
            payload += struct.pack("<H", len(ring))
            for lat, lon in ring:
                payload += struct.pack("<hh", lat, lon)
    south, west, north, east = grid.bounds(column, row)
    origin = grid.centre(column, row)
    header = struct.pack("<4sHHiiiiiiIIII", b"BMAP", 4, 0, origin[0], origin[1], south, west, north, east,
                         len(content["roads"]), point_count, len(payload), binascii.crc32(payload) & 0xFFFFFFFF)
    if len(content["roads"]) > 65535 or len(content["areas"]) > 65535 or len(names) > 65535:
        raise ValueError(f"tile {column},{row} has too many features")
    return header + bytes(payload)


def encode_archive(grid: Grid, blobs: dict[tuple[int, int], bytes]) -> bytes:
    """BTIL v1, byte-compatible with encode_tile_archive() in core/src/map_tiles.cpp."""
    keys = sorted(blobs, key=lambda key: (key[1], key[0]))
    header_size, entry_size = 40, 16
    offset = header_size + entry_size * len(keys)
    total = offset + sum(len(blobs[key]) for key in keys)
    index = bytearray()
    for column, row in keys:
        blob = blobs[(column, row)]
        index += struct.pack("<HHIII", column, row, offset, len(blob), binascii.crc32(blob) & 0xFFFFFFFF)
        offset += len(blob)
    head = struct.pack("<4sHHiiiiHHII", b"BTIL", 1, 0, grid.origin[0], grid.origin[1], grid.tile_lat,
                       grid.tile_lon, grid.columns, grid.rows, len(keys), total)
    crc = binascii.crc32(head + index) & 0xFFFFFFFF
    return head + struct.pack("<I", crc) + bytes(index) + b"".join(blobs[key] for key in keys)


# ---------------------------------------------------------------- main

def main() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=pathlib.Path, default=root / "data/amsterdam.btil")
    parser.add_argument("--metadata", type=pathlib.Path, default=root / "data/amsterdam.btil.json")
    parser.add_argument("--pbf", type=pathlib.Path,
                        help="read an OSM PBF extract (e.g. Geofabrik noord-holland) instead of Overpass")
    parser.add_argument("--cache-dir", type=pathlib.Path, help="save/reuse raw Overpass chunk responses here")
    parser.add_argument("--tile-m", type=float, default=500.0, help="tile edge in metres (default 500)")
    parser.add_argument("--chunk-deg", type=float, default=0.035, help="Overpass chunk height in degrees")
    parser.add_argument("--bbox", type=float, nargs=4, metavar=("S", "W", "N", "E"), default=AMSTERDAM_BBOX)
    parser.add_argument("--no-boundary", action="store_true",
                        help="keep every tile in the bbox instead of only tiles touching the municipality")
    parser.add_argument("--save-highways", type=pathlib.Path,
                        help="write the raw highway ways as Overpass JSON (the screen bench routes on it)")
    args = parser.parse_args()

    bbox = tuple(args.bbox)
    if args.pbf:
        elements, boundary, timestamp = read_pbf(args.pbf, bbox)
    else:
        elements, timestamp = fetch_city(bbox, args.chunk_deg, args.cache_dir)
        boundary = None
    grid = Grid(bbox, args.tile_m)
    if args.no_boundary:
        def keep_cell(column, row):
            return True
    else:
        if boundary is None:
            boundary = fetch_boundary(args.cache_dir)
        if not boundary:
            sys.exit("no Amsterdam boundary found; pass --no-boundary to keep the whole bbox")
        keep = set()
        for column in range(grid.columns):
            for row in range(grid.rows):
                s, w, n, e = (v / 1e7 for v in grid.bounds(column, row))
                samples = [(s + (n - s) * i / 4, w + (e - w) * j / 4) for i in range(5) for j in range(5)]
                if any(point_in_rings(p, boundary) for p in samples):
                    keep.add((column, row))
        for ring in boundary:  # tiles the boundary passes through
            for lat, lon in ring:
                keep.add(grid.cell(e7((lat, lon))))

        def keep_cell(column, row):
            return (column, row) in keep

    roads, areas = parse_features(elements)
    tiles = build_tiles(grid, roads, areas, keep_cell)
    blobs = {key: encode_bmap(grid, *key, content) for key, content in tiles.items()
             if content["roads"] or content["areas"]}
    archive = encode_archive(grid, blobs)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(archive)

    if args.save_highways:
        args.save_highways.parent.mkdir(parents=True, exist_ok=True)
        highways = [e for e in elements if e["type"] == "way" and e.get("tags", {}).get("highway") in ROAD_CLASS]
        args.save_highways.write_text(json.dumps({"osm3s": {"timestamp_osm_base": timestamp},
                                                  "elements": highways}, separators=(",", ":")))

    class_names = {0: "motorway", 1: "primary", 2: "secondary", 3: "residential", 4: "service",
                   5: "cycleway", 6: "footway"}
    by_class: dict[str, int] = {}
    for content in tiles.values():
        for _, road_class, _, points in content["roads"]:
            by_class[class_names[road_class]] = by_class.get(class_names[road_class], 0) + len(points)
    sizes = sorted(len(blob) for blob in blobs.values())
    metadata = {
        "source": f"OpenStreetMap contributors via {'Geofabrik extract ' + args.pbf.name if args.pbf else 'Overpass API'}",
        "license": "ODbL 1.0",
        "license_url": "https://www.openstreetmap.org/copyright",
        "generated_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
        "osm_timestamp": timestamp,
        "bbox": bbox,
        "tile_m": args.tile_m,
        "grid": {"origin_e7": grid.origin, "tile_lat_e7": grid.tile_lat, "tile_lon_e7": grid.tile_lon,
                 "columns": grid.columns, "rows": grid.rows},
        "tiles": len(blobs),
        "bytes": len(archive),
        "tile_bytes": {"min": sizes[0], "median": sizes[len(sizes) // 2], "max": sizes[-1]},
        "road_pieces": sum(len(t["roads"]) for t in tiles.values()),
        "area_pieces": sum(len(t["areas"]) for t in tiles.values()),
        "road_points_by_class": dict(sorted(by_class.items())),
    }
    args.metadata.write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))
    print(f"wrote {args.output}: {len(archive) / 1e6:.2f} MB in {len(blobs)} tiles", file=sys.stderr)


if __name__ == "__main__":
    main()
