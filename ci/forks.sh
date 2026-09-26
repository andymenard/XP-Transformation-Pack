#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# XPlasma's builds of Plasma's widgets, for CI. Each series in forks.json:
# its patched source (./build-forks.sh, FORKS_PREPARE_ONLY) hashed with what
# builds it -- its image's Containerfile and build-forks.sh. A build stored in
# the bucket under that hash is used as it is; otherwise the series is built
# (its image, then ./build-forks.sh -- the slow part, an hour or more) and
# stored for the next run.
#
#   ci/forks.sh      after ./build.sh release; needs TIGRIS_BUCKET
set -eu
here="$(cd "$(dirname "$0")/.." && pwd)"
cd "$here"
: "${TIGRIS_BUCKET:?which Tigris bucket}"
export CONTAINER="${CONTAINER:-docker}" XPLASMA="$here/ci/xplasma.sh"
base="${TIGRIS_PUBLIC_URL:?the bucket'"'"'s public address}"
for series in $(jq -r 'keys[]' src/app/overrides/forks.json); do
    FORKS_PREPARE_ONLY=1 ./build-forks.sh "$series"
    root="build/plasma/$series"
    key="$( { (cd "$root/work" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum)
              sha256sum "containers/plasma-$series.Containerfile" build-forks.sh; } | sha256sum | cut -c1-16)"
    object="cache/forks/$series-$key.tar.xz"
    rm -rf "$root/out"
    mkdir -p "$root/out"
    if curl -fsSL -o "$root/stored.tar.xz" "$base/$object"; then
        echo "forks $series: the stored build ($key)"
        tar xJf "$root/stored.tar.xz" -C "$root/out"
    else
        echo "forks $series: building ($key)"
        image="$("$XPLASMA" fork-info "$series" | awk '$1 == "image" { print $2 }')"
        "$CONTAINER" build -t "$image" -f "containers/plasma-$series.Containerfile" containers
        ./build-forks.sh "$series"
        tar cJf "$root/stored.tar.xz" -C "$root/out" plasma
        ci/tigris.sh cp "$root/stored.tar.xz" "t3://$TIGRIS_BUCKET/$object" --cache-control 'public, max-age=31536000, immutable'
    fi
done
