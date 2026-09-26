#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The release build's xplasma, run in the release image it was built in (it
# needs that image's Qt), with this checkout mounted at the same path, so the
# paths it's handed mean the same inside. For CI, where there's no dev build:
# build-forks.sh takes it as XPLASMA.
#
#   ci/xplasma.sh fork-info 6.8
set -eu
here="$(cd "$(dirname "$0")/.." && pwd)"
exec "${CONTAINER:-podman}" run --rm -v "$here:$here" -w "$PWD" -e LC_ALL=C.UTF-8 -e QT_QPA_PLATFORM=offscreen \
    localhost/xplasma-release:2 sh -c 'LD_LIBRARY_PATH=$QT6_DIR/lib exec "$0" "$@"' "$here/build/release/bin/xplasma" "$@"
