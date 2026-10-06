#!/usr/bin/env bash
# Builds GL1TCH OS with Docker and extracts the resulting floppy image to
# build/main_floppy.img, so run.sh (and debug.sh/bochs) boot what you just
# built. `docker build` alone only produces the gl1tch-os IMAGE - it never
# touches the host's build/ directory, which is what this script is for.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

docker build -t gl1tch-os .

mkdir -p build
id=$(docker create gl1tch-os)
docker cp "$id":/os/main_floppy.img ./build/main_floppy.img
docker rm "$id" >/dev/null

echo "Wrote build/main_floppy.img - now run ./run.sh"
