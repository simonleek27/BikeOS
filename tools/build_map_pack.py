#!/usr/bin/env python3
"""Build the flash-resident BikeOS map pack from BMAP tiles.

Each input BMAP is a bounded tile. The firmware keeps this container in flash
and decodes only tiles intersecting the rider's current view.
"""

from __future__ import annotations

import argparse
import binascii
import pathlib
import struct


def bmap_bounds(data: bytes) -> tuple[int, int, int, int]:
    if len(data) < 48 or data[:4] != b"BMAP":
        raise ValueError("invalid BMAP asset")
    return struct.unpack_from("<iiii", data, 16)[0], struct.unpack_from("<iiii", data, 20)[0], struct.unpack_from("<iiii", data, 24)[0], struct.unpack_from("<iiii", data, 28)[0]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("tiles", nargs="+", type=pathlib.Path)
    args = parser.parse_args()
    assets = [path.read_bytes() for path in args.tiles]
    index = bytearray()
    offset = 20 + 32 * len(assets)
    for data in assets:
        minimum_lat, minimum_lon, maximum_lat, maximum_lon = bmap_bounds(data)
        index += struct.pack("<iiiiIIII", minimum_lat, minimum_lon, maximum_lat, maximum_lon,
                             offset, len(data), binascii.crc32(data) & 0xFFFFFFFF, 0)
        offset += len(data)
    header = struct.pack("<4sHHIII", b"BPK1", 1, 0, len(assets), len(index), binascii.crc32(index) & 0xFFFFFFFF)
    args.output.write_bytes(header + index + b"".join(assets))
    print(f"wrote {args.output} ({offset} bytes, {len(assets)} tile(s))")


if __name__ == "__main__":
    main()
