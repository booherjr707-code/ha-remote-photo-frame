#!/bin/bash
# Double-click this file to shrink the photos in ~/Pictures/Photo Frame and copy
# them to the SD card for the remote's picture frame.
cd "$(dirname "$0")"

if [ ! -x .venv/bin/python ]; then
  echo "First run: installing the photo tools (about a minute)..."
  python3 -m venv .venv && .venv/bin/pip install --quiet pillow pillow-heif || {
    echo "Could not install the photo tools."; read -n 1 -p "Press any key to close."; exit 1; }
fi

.venv/bin/python tools/prepare_photos.py "$@"
echo
read -n 1 -p "Press any key to close."
