#!/usr/bin/env python3
"""Add water and park polygons to an existing BikeOS map, producing BMAP v4.

Roads (and therefore the demo route) are copied unchanged from the input map,
so this only adds the color layers the TFT renderer draws underneath them.

    python3 tools/add_map_areas.py                   # upgrade data/amsterdam_demo.bmap in place
    python3 tools/add_map_areas.py --input osm.json  # use a saved Overpass response
"""

from __future__ import annotations

import argparse
import binascii
import json
import pathlib
import struct
import subprocess

from prepare_amsterdam_demo import (
    BBOX,
    OVERPASS_FALLBACK_URL,
    OVERPASS_URL,
    USER_AGENT,
    QUANTIZATION_E7,
    e7,
    local_xy,
    simplify,
)

# Areas extend a little past the road extract so canals reach the screen edge.
MARGIN_DEG = 0.004
# Green patches smaller than this (verges, courtyard lawns) are dropped: they
# add clutter on a 2.8-inch screen and cost draw time. Water is always kept.
MIN_PARK_AREA_M2 = 2500.0
WATER, PARK = 0, 1


def classify(tags: dict) -> int | None:
    if tags.get("natural") == "water" or tags.get("waterway") == "riverbank" or tags.get("landuse") == "basin":
        return WATER
    if tags.get("leisure") in ("park", "garden") or tags.get("landuse") in (
        "grass", "recreation_ground", "village_green", "meadow", "forest"
    ) or tags.get("natural") == "wood":
        return PARK
    return None


def fetch_areas_osm() -> dict:
    south, west, north, east = BBOX
    box = f"({south - MARGIN_DEG},{west - MARGIN_DEG},{north + MARGIN_DEG},{east + MARGIN_DEG})"
    selectors = [
        '["natural"="water"]', '["waterway"="riverbank"]', '["landuse"="basin"]',
        '["leisure"="park"]', '["leisure"="garden"]["access"!="private"]',
        '["landuse"~"^(grass|recreation_ground|village_green|meadow|forest)$"]', '["natural"="wood"]',
    ]
    body = "".join(f"way{s}{box};relation{s}{box};" for s in selectors)
    query = f"[out:json][timeout:90];({body});out geom;"
    last_error = ""
    for endpoint in (OVERPASS_URL, OVERPASS_FALLBACK_URL):
        result = subprocess.run(
            ["curl", "--fail", "--silent", "--show-error", "--retry", "1",
             "-A", USER_AGENT, "--data-urlencode", f"data={query}", endpoint],
            check=False, capture_output=True,
        )
        if result.returncode == 0:
            return json.loads(result.stdout)
        last_error = result.stderr.decode().strip()
    raise RuntimeError(f"Overpass request failed: {last_error}")


def stitch(paths: list[list[tuple[float, float]]]) -> list[list[tuple[float, float]]]:
    """Join open member ways end to end into closed rings."""
    rings = []
    open_paths = [list(p) for p in paths if len(p) >= 2]
    while open_paths:
        current = open_paths.pop(0)
        while current[0] != current[-1]:
            for index, candidate in enumerate(open_paths):
                if candidate[0] == current[-1]:
                    current += candidate[1:]
                elif candidate[-1] == current[-1]:
                    current += list(reversed(candidate))[1:]
                elif candidate[-1] == current[0]:
                    current = candidate[:-1] + current
                elif candidate[0] == current[0]:
                    current = list(reversed(candidate))[:-1] + current
                else:
                    continue
                open_paths.pop(index)
                break
            else:
                break  # cannot close; the clip below still bounds it
        rings.append(current)
    return rings


def clip_ring(ring, south, west, north, east):
    """Sutherland-Hodgman clip of a lat/lon ring to a rectangle."""
    def clip(points, inside, intersect):
        output = []
        for i, current in enumerate(points):
            previous = points[i - 1]
            if inside(current):
                if not inside(previous):
                    output.append(intersect(previous, current))
                output.append(current)
            elif inside(previous):
                output.append(intersect(previous, current))
        return output

    def at_lat(a, b, lat):
        t = (lat - a[0]) / (b[0] - a[0])
        return (lat, a[1] + t * (b[1] - a[1]))

    def at_lon(a, b, lon):
        t = (lon - a[1]) / (b[1] - a[1])
        return (a[0] + t * (b[0] - a[0]), lon)

    points = ring[:-1] if len(ring) > 1 and ring[0] == ring[-1] else ring
    for inside, intersect in (
        (lambda p: p[0] >= south, lambda a, b: at_lat(a, b, south)),
        (lambda p: p[0] <= north, lambda a, b: at_lat(a, b, north)),
        (lambda p: p[1] >= west, lambda a, b: at_lon(a, b, west)),
        (lambda p: p[1] <= east, lambda a, b: at_lon(a, b, east)),
    ):
        if not points:
            break
        points = clip(points, inside, intersect)
    return points


def ring_area_m2(ring) -> float:
    origin = ring[0]
    points = [local_xy(point, origin) for point in ring]
    return abs(sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(points, points[1:] + points[:1]))) / 2.0


def parse_areas(osm: dict) -> list[dict]:
    south, west, north, east = BBOX
    bounds = (south - MARGIN_DEG, west - MARGIN_DEG, north + MARGIN_DEG, east + MARGIN_DEG)
    areas = []
    for element in osm.get("elements", []):
        tags = element.get("tags", {})
        area_class = classify(tags)
        if area_class is None:
            continue
        if element["type"] == "way":
            outer = [[(p["lat"], p["lon"]) for p in element.get("geometry", [])]]
            inner = []
        else:
            members = element.get("members", [])
            geometry = lambda role: [
                [(p["lat"], p["lon"]) for p in m.get("geometry", [])]
                for m in members if m.get("type") == "way" and m.get("role", "outer") == role
            ]
            outer = stitch(geometry("outer") + geometry(""))
            inner = stitch(geometry("inner"))
        # Each outer ring becomes its own polygon; inner rings go with the
        # first polygon, and the even-odd fill punches them out.
        for index, ring in enumerate(outer):
            clipped = clip_ring(ring, *bounds)
            simplified = simplify(clipped + clipped[:1], 1.0)[:-1] if len(clipped) >= 3 else []
            if len(simplified) < 3:
                continue
            if area_class == PARK and ring_area_m2(simplified) < MIN_PARK_AREA_M2:
                continue
            rings = [simplified]
            if index == 0:
                for hole in inner:
                    clipped_hole = clip_ring(hole, *bounds)
                    simplified_hole = simplify(clipped_hole + clipped_hole[:1], 1.0)[:-1]
                    if len(simplified_hole) >= 3:
                        rings.append(simplified_hole)
            areas.append({"class": area_class, "name": tags.get("name", ""), "rings": rings[:255]})
    # Parks first so water drawn later wins where they overlap (ponds in parks).
    areas.sort(key=lambda area: area["class"] == WATER)
    return areas


def read_map(data: bytes) -> dict:
    header = struct.unpack_from("<4sHHiiiiiiIIII", data, 0)
    magic, version, flags, olat, olon, slat, slon, nlat, nlon, road_count, point_count, size, crc = header
    if magic != b"BMAP" or version != 3:
        raise ValueError(f"expected a BMAP v3 input, got {magic!r} v{version}")
    offset = struct.calcsize("<4sHHiiiiiiIIII")
    payload = data[offset:]
    if len(payload) != size or binascii.crc32(payload) & 0xFFFFFFFF != crc:
        raise ValueError("input map payload is corrupt")
    cursor = 0
    (name_count,) = struct.unpack_from("<H", payload, cursor)
    cursor += 2
    names = []
    for _ in range(name_count):
        (length,) = struct.unpack_from("<B", payload, cursor)
        cursor += 1
        names.append(payload[cursor:cursor + length].decode("utf-8"))
        cursor += length
    return {
        "origin": (olat, olon), "bounds": (slat, slon, nlat, nlon),
        "names": names, "road_count": road_count, "point_count": point_count,
        "roads_bytes": payload[cursor:],
    }


def write_map(source: dict, areas: list[dict]) -> bytes:
    names = list(source["names"])
    for area in areas:
        if area["name"] not in names:
            names.append(area["name"])
    if len(names) > 65535:
        raise ValueError("too many names")
    payload = bytearray(struct.pack("<H", len(names)))
    for name in names:
        encoded = name.encode("utf-8")[:96]
        payload += struct.pack("<B", len(encoded)) + encoded
    payload += source["roads_bytes"]
    payload += struct.pack("<H", len(areas))
    origin = source["origin"]
    for area in areas:
        payload += struct.pack("<BHB", area["class"], names.index(area["name"]), len(area["rings"]))
        for ring in area["rings"]:
            payload += struct.pack("<H", len(ring))
            for point in ring:
                latitude, longitude = e7(point)
                lat_offset = round((latitude - origin[0]) / QUANTIZATION_E7)
                lon_offset = round((longitude - origin[1]) / QUANTIZATION_E7)
                if not (-32768 <= lat_offset <= 32767 and -32768 <= lon_offset <= 32767):
                    raise ValueError("area point outside the int16 corridor")
                payload += struct.pack("<hh", lat_offset, lon_offset)
    header = struct.pack(
        "<4sHHiiiiiiIIII", b"BMAP", 4, 0, *origin, *source["bounds"],
        source["road_count"], source["point_count"], len(payload), binascii.crc32(payload) & 0xFFFFFFFF,
    )
    return header + bytes(payload)


def main() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", type=pathlib.Path, default=root / "data/amsterdam_demo.bmap")
    parser.add_argument("--source-map", type=pathlib.Path,
                        help="v3 map to read roads from (default: --map, which must still be v3)")
    parser.add_argument("--input", type=pathlib.Path, help="saved Overpass JSON with the area query")
    parser.add_argument("--save-source", type=pathlib.Path, help="save the raw Overpass response")
    args = parser.parse_args()

    source = read_map((args.source_map or args.map).read_bytes())
    osm = json.loads(args.input.read_text()) if args.input else fetch_areas_osm()
    if args.save_source:
        args.save_source.write_text(json.dumps(osm, separators=(",", ":")))
    areas = parse_areas(osm)
    output = write_map(source, areas)
    args.map.write_bytes(output)
    water = sum(1 for a in areas if a["class"] == WATER)
    points = sum(len(r) for a in areas for r in a["rings"])
    print(f"wrote {args.map}: {len(areas)} areas ({water} water, {len(areas) - water} park), "
          f"{points} area points, {len(output)} bytes")


if __name__ == "__main__":
    main()
