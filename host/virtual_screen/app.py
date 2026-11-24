#!/usr/bin/env python3
"""BikeOS screen test bench as a native macOS window.

Builds bikeos_virtual_screen if its sources changed, starts the bench server on
a free localhost port, and shows the page in a pywebview window (WebKit, the
same engine as Safari). Closing the window stops the server and the engine.

    host/virtual_screen/bench.sh          # sets up the venv on first run
    open "host/virtual_screen/BikeOS Bench.app"
"""

from __future__ import annotations

import html
import os
import pathlib
import shutil
import subprocess
import threading

import webview

import server

ROOT = server.ROOT
BUILD_DIR = ROOT / "build-host"
TITLE = "BikeOS Screen Bench"


def find_cmake() -> str | None:
    found = shutil.which("cmake")
    if found:
        return found
    # ESP-IDF ships a cmake that works for the host build too.
    for candidate in sorted(pathlib.Path.home().glob(".espressif/tools/cmake/*/CMake.app/Contents/bin/cmake")):
        return str(candidate)
    return None


def build_engine() -> None:
    """Configures and builds the engine target; a no-op when it is up to date."""
    cmake = find_cmake()
    if cmake is None:
        if server.DEFAULT_ENGINE.exists():
            return  # no toolchain, but an engine binary is already there
        raise RuntimeError("cmake not found, and the engine has not been built yet.")
    if not (BUILD_DIR / "CMakeCache.txt").exists():
        subprocess.run([cmake, "-S", str(ROOT), "-B", str(BUILD_DIR), "-DCMAKE_BUILD_TYPE=Release"],
                       check=True, capture_output=True, text=True)
    result = subprocess.run([cmake, "--build", str(BUILD_DIR), "--target", "bikeos_virtual_screen", "-j", "8"],
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("Engine build failed:\n\n" + (result.stdout + result.stderr)[-4000:])


def error_window(message: str) -> None:
    page = f"""<body style="font:14px -apple-system,sans-serif;padding:24px;background:#1c1c1e;color:#f2f2f7">
<h2 style="margin-top:0">The screen bench could not start</h2>
<pre style="white-space:pre-wrap;background:#000;padding:12px;border-radius:8px">{html.escape(message)}</pre>
</body>"""
    webview.create_window(TITLE, html=page, width=720, height=480)
    webview.start()


def name_process() -> None:
    """Show the bench's name in the Dock and menu bar instead of "Python"."""
    try:
        from Foundation import NSBundle
        info = NSBundle.mainBundle().localizedInfoDictionary() or NSBundle.mainBundle().infoDictionary()
        info["CFBundleName"] = TITLE
    except Exception:
        pass


def main() -> None:
    os.chdir(ROOT)
    name_process()
    try:
        build_engine()
        bench = server.create_server(port=0)
    except (RuntimeError, FileNotFoundError, subprocess.CalledProcessError) as error:
        detail = getattr(error, "stderr", None) or str(error)
        error_window(detail)
        return

    port = bench.server_address[1]
    threading.Thread(target=bench.serve_forever, daemon=True).start()

    # Let the "Save screenshot" button download into ~/Downloads.
    webview.settings["ALLOW_DOWNLOADS"] = True
    window = webview.create_window(
        TITLE, f"http://127.0.0.1:{port}/",
        width=1440, height=940, min_size=(900, 640), background_color="#000000",
    )
    window.events.closed += lambda: server.shutdown(bench)
    webview.start()


if __name__ == "__main__":
    main()
