#!/usr/bin/env python3
"""BikeOS screen test bench.

Serves a web page with a pixel-exact replica of the 320x240 TFT, test
controls around it, and an iPhone-style panel for picking a route. Frames come
from `bikeos_virtual_screen`, which runs the real C++ renderer and navigation
engine the way the ESP32 firmware does.

    cmake --build build-host --target bikeos_virtual_screen
    python3 host/virtual_screen/server.py            # http://localhost:8765
"""

from __future__ import annotations

import argparse
import itertools
import json
import pathlib
import subprocess
import sys
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
CACHE = HERE / ".cache"
sys.path.insert(0, str(ROOT / "tools"))

import prepare_amsterdam_demo as osm  # noqa: E402  (shared OSM routing helpers)

# Places inside the bundled Amsterdam map, for the destination search.
PLACES = [
    {"name": "Rijksmuseum", "lat": 52.36000, "lon": 4.88520},
    {"name": "Van Gogh Museum", "lat": 52.35840, "lon": 4.88110},
    {"name": "Museumplein", "lat": 52.35730, "lon": 4.88200},
    {"name": "Concertgebouw", "lat": 52.35630, "lon": 4.87890},
    {"name": "Heineken Experience", "lat": 52.35780, "lon": 4.89180},
    {"name": "Vondelpark (east gate)", "lat": 52.36100, "lon": 4.87900},
    {"name": "Leidseplein", "lat": 52.36370, "lon": 4.88260},
    {"name": "Albert Cuyp Market", "lat": 52.35580, "lon": 4.89310},
    {"name": "Demo start (Overtoom)", "lat": osm.START[0], "lon": osm.START[1]},
    {"name": "Demo destination", "lat": osm.DESTINATION[0], "lon": osm.DESTINATION[1]},
]
# Where the phone thinks it is when "Current location" is selected.
CURRENT_LOCATION = {"lat": osm.START[0], "lon": osm.START[1]}


class Engine:
    """Owns the bikeos_virtual_screen process; one request at a time."""

    def __init__(self, binary: pathlib.Path):
        self.process = subprocess.Popen(
            [str(binary), str(ROOT / "data")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0,
        )
        self.lock = threading.Lock()

    def _read_exact(self, count: int) -> bytes:
        chunks = []
        while count:
            chunk = self.process.stdout.read(count)
            if not chunk:
                raise RuntimeError("virtual screen engine exited")
            chunks.append(chunk)
            count -= len(chunk)
        return b"".join(chunks)

    def _read_line(self) -> list[str]:
        line = bytearray()
        while not line.endswith(b"\n"):
            line += self._read_exact(1)
        return line.decode().split()

    def call(self, line: str):
        with self.lock:
            self.process.stdin.write((line + "\n").encode())
            self.process.stdin.flush()
            header = self._read_line()
            if header[0] == "JSON":
                return json.loads(self._read_exact(int(header[1]))), None
            status = json.loads(self._read_exact(int(header[1])))
            pixels = self._read_exact(int(header[2]))
            return status, pixels


class Router:
    """Bicycle routing over the OSM street graph for the bundled map area."""

    def __init__(self):
        self.roads = None
        self.lock = threading.Lock()
        self.ids = itertools.count(1)
        self.routes: dict[str, pathlib.Path] = {}

    def _load_roads(self):
        if self.roads is not None:
            return self.roads
        cache = CACHE / "osm_highways.json"
        if cache.exists():
            data = json.loads(cache.read_text())
        else:
            CACHE.mkdir(exist_ok=True)
            data = osm.fetch_osm()  # one Overpass request, then cached
            cache.write_text(json.dumps(data, separators=(",", ":")))
        self.roads = osm.parse_roads(data)
        return self.roads

    def route(self, start, destination) -> dict:
        with self.lock:
            roads = self._load_roads()
            points, streets = osm.build_route(roads, tuple(start), tuple(destination))
            maneuvers = osm.route_maneuvers(points, streets)
            route_id = 0xB1000000 | next(self.ids)
            data = osm.encode_route(points, maneuvers, route_id)
            token = f"{route_id:08x}"
            path = CACHE / f"route-{token}.brte"
            path.write_bytes(data)
            self.routes[token] = path
        distance = sum(osm.haversine_m(a, b) for a, b in zip(points, points[1:]))
        names = {0: "straight", 1: "left", 2: "right", 3: "slight left", 4: "slight right",
                 5: "sharp left", 6: "sharp right", 7: "U-turn", 8: "roundabout", 9: "arrive"}
        return {
            "token": token,
            "route_id": route_id,
            "points": [[round(lat, 6), round(lon, 6)] for lat, lon in points],
            "distance_m": round(distance, 1),
            "duration_min": max(1, round(distance / (15 / 3.6) / 60)),
            "maneuvers": [{"index": i, "type": names.get(t, str(t)), "street": s} for i, t, s in maneuvers],
        }


class Handler(BaseHTTPRequestHandler):
    engine: Engine
    router: Router
    map_cache: bytes | None = None

    def log_message(self, *_):  # keep the console quiet; frames are frequent
        pass

    def _send(self, body: bytes, content_type: str, status=HTTPStatus.OK, headers=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(body)

    def _json(self, value, status=HTTPStatus.OK):
        self._send(json.dumps(value).encode(), "application/json", status)

    def _body(self) -> dict:
        length = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(length) or b"{}")

    def do_GET(self):
        url = urlparse(self.path)
        if url.path in ("/", "/index.html"):
            return self._send((HERE / "index.html").read_bytes(), "text/html; charset=utf-8")
        if url.path in ("/bench.js", "/bench.css"):
            kind = "text/javascript" if url.path.endswith(".js") else "text/css"
            return self._send((HERE / url.path[1:]).read_bytes(), kind + "; charset=utf-8")
        if url.path == "/api/frame":
            # The engine's clock is the server's monotonic time, like esp_timer on
            # the device; a browser clock would restart on every page reload.
            now = int(time.monotonic() * 1000)
            status, pixels = self.engine.call(f"frame {now}")
            return self._send(pixels, "application/octet-stream", headers={"X-Status": json.dumps(status)})
        if url.path == "/api/map":
            if Handler.map_cache is None:
                Handler.map_cache = json.dumps(self.engine.call("map")[0]).encode()
            return self._send(Handler.map_cache, "application/json")
        if url.path == "/api/places":
            return self._json({"places": PLACES, "current": CURRENT_LOCATION})
        self._json({"error": "not found"}, HTTPStatus.NOT_FOUND)

    def do_POST(self):
        url = urlparse(self.path)
        try:
            body = self._body()
            if url.path == "/api/set":
                key, value = str(body["key"]), str(body["value"])
                if not key.isidentifier() or " " in value:
                    raise ValueError("bad setting")
                return self._json(self.engine.call(f"set {key} {value}")[0])
            if url.path == "/api/seek":
                return self._json(self.engine.call(f"seek {float(body['metres']):.2f}")[0])
            if url.path == "/api/command":
                name = str(body["name"])
                if name not in ("start", "pause", "resume", "stop", "restart", "offroute"):
                    raise ValueError("unknown command")
                return self._json(self.engine.call(f"command {name}")[0])
            if url.path == "/api/route":
                return self._json(self.router.route(body["start"], body["destination"]))
            if url.path == "/api/send":
                token = str(body.get("token", "demo"))
                with_map = 1 if body.get("with_map") else 0
                if token == "demo":
                    path, with_map = ROOT / "data" / "amsterdam_demo.brte", 1  # the app sends the BMAP with the demo
                else:
                    path = self.router.routes.get(token)
                    if path is None:
                        raise ValueError("unknown route; calculate it again")
                return self._json(self.engine.call(f"install {with_map} {path}")[0])
        except Exception as error:  # surface every failure to the page
            return self._json({"error": str(error)}, HTTPStatus.BAD_REQUEST)
        self._json({"error": "not found"}, HTTPStatus.NOT_FOUND)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--engine", type=pathlib.Path, default=ROOT / "build-host" / "bikeos_virtual_screen")
    args = parser.parse_args()
    if not args.engine.exists():
        sys.exit(f"{args.engine} not found; build it with: cmake --build build-host --target bikeos_virtual_screen")
    Handler.engine = Engine(args.engine)
    Handler.router = Router()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"BikeOS screen test bench on http://localhost:{args.port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
