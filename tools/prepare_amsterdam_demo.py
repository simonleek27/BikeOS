#!/usr/bin/env python3
"""Fetch a small OSM extract and build deterministic BikeOS map/route assets.

The public Overpass endpoint is used for this one-off development extract only.
Production clients must use a suitable hosted provider or regional extracts.
"""

from __future__ import annotations

import argparse
import binascii
import datetime as dt
import heapq
import json
import math
import pathlib
import struct
import subprocess
import urllib.parse
import urllib.request
import urllib.error

OVERPASS_URL = "https://overpass-api.de/api/interpreter"
OVERPASS_FALLBACK_URL = "https://ovp.dresel.systems/api/interpreter"
BBOX = (52.3548, 4.8775, 52.3640, 4.8940)
START = (52.36102, 4.87965)
DESTINATION = (52.35670, 4.89120)
USER_AGENT = "BikeOS/0.2 map-preprocessor (https://github.com/simonleek27/BikeOS)"
QUANTIZATION_E7 = 10

ROAD_CLASS = {
    "motorway": 0,
    "trunk": 1,
    "primary": 1,
    "secondary": 2,
    "tertiary": 2,
    "unclassified": 3,
    "residential": 3,
    "living_street": 3,
    "service": 4,
    "cycleway": 5,
    "path": 5,
    "footway": 6,
    "pedestrian": 6,
    "steps": 6,
}


def haversine_m(a: tuple[float, float], b: tuple[float, float]) -> float:
    radius = 6_371_008.8
    lat1, lon1 = map(math.radians, a)
    lat2, lon2 = map(math.radians, b)
    dlat = lat2 - lat1
    dlon = lon2 - lon1
    value = math.sin(dlat / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon / 2) ** 2
    return 2 * radius * math.asin(math.sqrt(value))


def local_xy(point: tuple[float, float], origin: tuple[float, float]) -> tuple[float, float]:
    radius = 6_371_008.8
    return (
        math.radians(point[1] - origin[1]) * math.cos(math.radians(origin[0])) * radius,
        math.radians(point[0] - origin[0]) * radius,
    )


def perpendicular_distance(point, start, end) -> float:
    px, py = local_xy(point, start)
    ex, ey = local_xy(end, start)
    length_sq = ex * ex + ey * ey
    if length_sq == 0:
        return math.hypot(px, py)
    t = max(0.0, min(1.0, (px * ex + py * ey) / length_sq))
    return math.hypot(px - t * ex, py - t * ey)


def simplify(points: list[tuple[float, float]], tolerance_m: float) -> list[tuple[float, float]]:
    if len(points) <= 2:
        return points
    maximum = 0.0
    split = 0
    for index in range(1, len(points) - 1):
        distance = perpendicular_distance(points[index], points[0], points[-1])
        if distance > maximum:
            maximum = distance
            split = index
    if maximum <= tolerance_m:
        return [points[0], points[-1]]
    left = simplify(points[: split + 1], tolerance_m)
    right = simplify(points[split:], tolerance_m)
    return left[:-1] + right


def fetch_osm() -> dict:
    south, west, north, east = BBOX
    query = f"""
    [out:json][timeout:60];
    way[highway]({south},{west},{north},{east});
    out geom;
    """
    body = urllib.parse.urlencode({"data": query}).encode()
    request = urllib.request.Request(OVERPASS_URL, data=body, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=90) as response:
            return json.load(response)
    except urllib.error.URLError as error:
        # Apple's bundled Python can lag current TLS settings. Curl is present on
        # macOS and preserves the same bounded POST request and identifying UA.
        last_error = ""
        for endpoint in (OVERPASS_URL, OVERPASS_FALLBACK_URL):
            result = subprocess.run(
                [
                    "curl", "--fail", "--silent", "--show-error", "--retry", "1",
                    "-A", USER_AGENT, "--data-urlencode", f"data={query}", endpoint,
                ],
                check=False,
                capture_output=True,
            )
            if result.returncode == 0:
                return json.loads(result.stdout)
            last_error = result.stderr.decode().strip()
        raise RuntimeError(f"Overpass request failed: {last_error}") from error


def parse_roads(osm: dict):
    roads = []
    for element in osm.get("elements", []):
        highway = element.get("tags", {}).get("highway")
        geometry = element.get("geometry", [])
        if highway not in ROAD_CLASS or len(geometry) < 2:
            continue
        points = [(float(point["lat"]), float(point["lon"])) for point in geometry]
        roads.append(
            {
                "id": int(element["id"]),
                "class": ROAD_CLASS[highway],
                "highway": highway,
                "name": element.get("tags", {}).get("name", highway),
                "oneway": element.get("tags", {}).get("oneway") == "yes",
                "points": points,
            }
        )
    if not roads:
        raise RuntimeError("Overpass response contained no usable highway geometry")
    return roads


def nearest_node(nodes, target):
    return min(nodes, key=lambda node: haversine_m(node, target))


def build_route(roads):
    graph: dict[tuple[float, float], list[tuple[float, tuple[float, float], str]]] = {}
    for road in roads:
        if road["class"] in (0, 6) or road["highway"] == "steps":
            continue
        for a, b in zip(road["points"], road["points"][1:]):
            weight = haversine_m(a, b)
            graph.setdefault(a, []).append((weight, b, road["name"]))
            if not road["oneway"]:
                graph.setdefault(b, []).append((weight, a, road["name"]))
    start = nearest_node(graph, START)
    destination = nearest_node(graph, DESTINATION)
    queue = [(0.0, start)]
    distance = {start: 0.0}
    previous: dict[tuple[float, float], tuple[tuple[float, float], str]] = {}
    while queue:
        current_distance, current = heapq.heappop(queue)
        if current == destination:
            break
        if current_distance != distance[current]:
            continue
        for weight, neighbor, name in graph.get(current, []):
            candidate = current_distance + weight
            if candidate < distance.get(neighbor, math.inf):
                distance[neighbor] = candidate
                previous[neighbor] = (current, name)
                heapq.heappush(queue, (candidate, neighbor))
    if destination not in previous:
        raise RuntimeError("Could not find a connected bicycle route in the OSM extract")
    points = [destination]
    street_names = []
    cursor = destination
    while cursor != start:
        cursor, name = previous[cursor]
        points.append(cursor)
        street_names.append(name)
    points.reverse()
    street_names.reverse()
    return points, street_names


def signed_turn_degrees(a, b, c) -> float:
    incoming = local_xy(b, a)
    outgoing = local_xy(c, b)
    cross = incoming[0] * outgoing[1] - incoming[1] * outgoing[0]
    dot = incoming[0] * outgoing[0] + incoming[1] * outgoing[1]
    return math.degrees(math.atan2(cross, dot))


def route_maneuvers(points, street_names):
    maneuvers = []
    last_maneuver = -10
    for index in range(1, len(points) - 1):
        turn = signed_turn_degrees(points[index - 1], points[index], points[index + 1])
        if abs(turn) < 28 or index - last_maneuver < 2:
            continue
        magnitude = abs(turn)
        if turn > 0:
            maneuver_type = 3 if magnitude < 50 else (1 if magnitude < 120 else 5)
        else:
            maneuver_type = 4 if magnitude < 50 else (2 if magnitude < 120 else 6)
        maneuvers.append((index, maneuver_type, street_names[min(index, len(street_names) - 1)]))
        last_maneuver = index
    maneuvers.append((len(points) - 1, 9, street_names[-1] if street_names else ""))
    return maneuvers


def e7(point):
    return round(point[0] * 10_000_000), round(point[1] * 10_000_000)


def quantized_offset(value, origin):
    offset = round((value - origin) / QUANTIZATION_E7)
    if not -32768 <= offset <= 32767:
        raise ValueError("OSM extract exceeds int16 coordinate corridor")
    return offset


def encode_map(roads):
    south, west, north, east = BBOX
    origin = e7(((south + north) / 2, (west + east) / 2))
    payload = bytearray()
    point_count = 0
    encoded_roads = []
    names = []
    for road in roads:
        points = simplify(road["points"], 1.5)
        if len(points) >= 2:
            name = road["name"]
            name = name.encode("utf-8")[:96].decode("utf-8", errors="ignore")
            if name not in names:
                names.append(name)
            encoded_roads.append((road["id"], road["class"], names.index(name), points))
    payload += struct.pack("<H", len(names))
    for name in names:
        name_bytes = name.encode("utf-8")
        payload += struct.pack("<B", len(name_bytes))
        payload += name_bytes
    for feature_id, road_class, name_index, points in encoded_roads:
        payload += struct.pack("<BIHH", road_class, feature_id, len(points), name_index)
        for point in points:
            latitude, longitude = e7(point)
            payload += struct.pack(
                "<hh",
                quantized_offset(latitude, origin[0]),
                quantized_offset(longitude, origin[1]),
            )
        point_count += len(points)
    header = struct.pack(
        "<4sHHiiiiiiIIII",
        b"BMAP", 3, 0,
        origin[0], origin[1],
        round(south * 1e7), round(west * 1e7), round(north * 1e7), round(east * 1e7),
        len(encoded_roads), point_count, len(payload), binascii.crc32(payload) & 0xFFFFFFFF,
    )
    return header + payload, len(encoded_roads), point_count


def encode_route(points, maneuvers):
    origin = e7(points[0])
    payload = bytearray()
    for point in points:
        latitude, longitude = e7(point)
        payload += struct.pack(
            "<hh",
            quantized_offset(latitude, origin[0]),
            quantized_offset(longitude, origin[1]),
        )
    for point_index, maneuver_type, street_name in maneuvers:
        name = street_name.encode("utf-8")[:96]
        payload += struct.pack("<HBB", point_index, maneuver_type, len(name)) + name
    header = struct.pack(
        "<4sHHIiiHHII",
        b"BRTE", 1, 0, 0xA05D0001, origin[0], origin[1], len(points), len(maneuvers),
        len(payload), binascii.crc32(payload) & 0xFFFFFFFF,
    )
    return header + payload


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=pathlib.Path, help="Use a saved Overpass JSON response")
    parser.add_argument("--output-dir", type=pathlib.Path, default=pathlib.Path("data"))
    parser.add_argument("--save-source", type=pathlib.Path, help="Optionally save the raw response")
    args = parser.parse_args()

    osm = json.loads(args.input.read_text()) if args.input else fetch_osm()
    if args.save_source:
        args.save_source.write_text(json.dumps(osm, separators=(",", ":")))
    roads = parse_roads(osm)
    route_points, street_names = build_route(roads)
    maneuvers = route_maneuvers(route_points, street_names)
    map_bytes, road_count, point_count = encode_map(roads)
    route_bytes = encode_route(route_points, maneuvers)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "amsterdam_demo.bmap").write_bytes(map_bytes)
    (args.output_dir / "amsterdam_demo.brte").write_bytes(route_bytes)
    metadata = {
        "source": "OpenStreetMap contributors via Overpass API",
        "source_url": OVERPASS_URL,
        "license": "ODbL 1.0",
        "license_url": "https://www.openstreetmap.org/copyright",
        "generated_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "osm_timestamp": osm.get("osm3s", {}).get("timestamp_osm_base"),
        "bbox": BBOX,
        "road_polylines": road_count,
        "map_points": point_count,
        "route_points": len(route_points),
        "route_distance_m": round(sum(haversine_m(a, b) for a, b in zip(route_points, route_points[1:])), 1),
        "maneuvers": len(maneuvers),
    }
    (args.output_dir / "amsterdam_demo.metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
