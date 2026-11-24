#!/usr/bin/env python3
"""Build a tiled offline Amsterdam BMAP pack from one bounded OSM extract."""

from __future__ import annotations

import argparse
import importlib.util
import json
import pathlib
import tempfile


def load_demo_module():
    path = pathlib.Path(__file__).with_name("prepare_amsterdam_demo.py")
    spec = importlib.util.spec_from_file_location("bikeos_demo_builder", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load map builder")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bbox", nargs=4, type=float, metavar=("SOUTH", "WEST", "NORTH", "EAST"),
                        default=(52.30, 4.75, 52.42, 5.00))
    parser.add_argument("--tile-degrees", type=float, default=0.015)
    parser.add_argument("--input", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("data/amsterdam_map.pack"))
    parser.add_argument("--source", type=pathlib.Path, help="save raw OSM JSON for reproducible rebuilds")
    args = parser.parse_args()
    builder = load_demo_module()
    builder.BBOX = tuple(args.bbox)
    osm = json.loads(args.input.read_text()) if args.input else builder.fetch_osm()
    if args.source:
        args.source.write_text(json.dumps(osm, separators=(",", ":")))
    roads = builder.parse_roads(osm)
    south, west, north, east = builder.BBOX
    tiles: list[pathlib.Path] = []
    with tempfile.TemporaryDirectory(prefix="bikeos-map-tiles-") as directory:
        root = pathlib.Path(directory)
        lat = south
        while lat < north:
            lon = west
            tile_north = min(lat + args.tile_degrees, north)
            while lon < east:
                tile_east = min(lon + args.tile_degrees, east)
                selected = []
                for road in roads:
                    segment = []
                    for point in road["points"]:
                        inside = lat <= point[0] <= tile_north and lon <= point[1] <= tile_east
                        if inside:
                            segment.append(point)
                        elif len(segment) >= 2:
                            clipped = dict(road)
                            clipped["points"] = segment
                            selected.append(clipped)
                            segment = []
                        else:
                            segment = []
                    if len(segment) >= 2:
                        clipped = dict(road)
                        clipped["points"] = segment
                        selected.append(clipped)
                if selected:
                    builder.BBOX = (lat, lon, tile_north, tile_east)
                    encoded, _, _ = builder.encode_map(selected)
                    path = root / f"tile-{len(tiles):04d}.bmap"
                    path.write_bytes(encoded)
                    tiles.append(path)
                lon += args.tile_degrees
            lat += args.tile_degrees
        if not tiles:
            raise RuntimeError("OSM extract produced no map tiles")
        import subprocess
        subprocess.run(["python3", str(pathlib.Path(__file__).with_name("build_map_pack.py")), str(args.output), *map(str, tiles)], check=True)
    print(f"built {len(tiles)} offline Amsterdam tiles")


if __name__ == "__main__":
    main()
