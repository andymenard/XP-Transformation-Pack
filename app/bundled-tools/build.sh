#!/bin/sh
# Build-time only: nothing here runs during a user's installation.
set -eu
TOOLS_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOOLS_ENGINE=${TOOLS_ENGINE:-podman}
TOOLS_ARCH=$(uname -m)
python3 "$TOOLS_ROOT/build.py" --fetch
"$TOOLS_ENGINE" build -t localhost/xp2plasma-tools -f "$TOOLS_ROOT/Containerfile" "$TOOLS_ROOT"
mkdir -p "$TOOLS_ROOT/$TOOLS_ARCH"
"$TOOLS_ENGINE" run --rm --network=none \
    -v "$TOOLS_ROOT:/src:ro" -v "$TOOLS_ROOT/$TOOLS_ARCH:/out:rw" \
    localhost/xp2plasma-tools
