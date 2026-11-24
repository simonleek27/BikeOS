#!/bin/zsh
# Opens the BikeOS screen bench in its own window. The first run creates a
# private Python environment with pywebview in host/virtual_screen/.venv.
set -e
HERE="${0:A:h}"
VENV="$HERE/.venv"
if [[ ! -x "$VENV/bin/python" ]]; then
    echo "Setting up the screen bench (first run only)..."
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install --quiet --upgrade pip
    "$VENV/bin/pip" install --quiet -r "$HERE/requirements.txt"
fi
exec "$VENV/bin/python" "$HERE/app.py" "$@"
