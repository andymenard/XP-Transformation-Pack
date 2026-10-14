#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# XPlasma's builds of Plasma's widgets, for CI: every series in forks.json
# at once, each in a process of its own, its lines marked with its series
# ("[6.8] ..."). The machine's compile jobs (JOBS, worked out as
# build-forks.sh does) are shared out between them, so together they take
# no more memory than one alone.
#
# Each series: its patched source (./build-forks.sh, FORKS_PREPARE_ONLY)
# hashed with what builds it -- its image's Containerfile and
# build-forks.sh. A build stored in the bucket under that hash is used as
# it is; otherwise the series is built (its image, then ./build-forks.sh)
# and stored for the next run.
#
#   ci/forks.sh [SERIES...]     all of forks.json's by default; after
#                               ./build.sh release; needs TIGRIS_BUCKET and
#                               TIGRIS_PUBLIC_URL
set -eu
here="$(cd "$(dirname "$0")/.." && pwd)"
cd "$here"
: "${TIGRIS_BUCKET:?which Tigris bucket}"
base="${TIGRIS_PUBLIC_URL:?the bucket's public address}"
export CONTAINER="${CONTAINER:-docker}" XPLASMA="$here/ci/xplasma.sh"

# One series (ci/forks.sh --one SERIES): the stored build, or a new one,
# stored.
if [ "${1:-}" = --one ]; then
    series="$2"
    FORKS_PREPARE_ONLY=1 ./build-forks.sh "$series"
    root="build/plasma/$series"
    key="$( { (cd "$root/work" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum)
              sha256sum "containers/plasma-$series.Containerfile" build-forks.sh; } | sha256sum | cut -c1-16)"
    object="cache/forks/$series-$key.tar.xz"
    rm -rf "$root/out"
    mkdir -p "$root/out"
    if curl -fsSL -o "$root/stored.tar.xz" "$base/$object"; then
        echo "the stored build ($key)"
        tar xJf "$root/stored.tar.xz" -C "$root/out"
    else
        echo "building ($key)"
        image="$("$XPLASMA" fork-info "$series" | awk '$1 == "image" { print $2 }')"
        "$CONTAINER" build -t "$image" -f "containers/plasma-$series.Containerfile" containers
        ./build-forks.sh "$series"
        tar cJf "$root/stored.tar.xz" -C "$root/out" plasma
        ci/tigris.sh cp "$root/stored.tar.xz" "t3://$TIGRIS_BUCKET/$object" --cache-control 'public, max-age=31536000, immutable'
    fi
    exit 0
fi

[ $# -gt 0 ] || set -- $(jq -r 'keys[]' src/app/overrides/forks.json)
# (the Tigris CLI fetched and signed in once, before the series need it
# together -- and the key checked now, not after the compiling)
ci/tigris.sh credentials test --bucket "$TIGRIS_BUCKET"
# The compile jobs: about a gigabyte each, no more than the processors,
# shared out.
all="${JOBS:-$(awk '/MemAvailable/ { print int($2 / 1100000) }' /proc/meminfo)}"
[ "$all" -le "$(nproc)" ] || all=$(nproc)
JOBS=$((all / $#))
[ "$JOBS" -ge 1 ] || JOBS=1
export JOBS
echo "Plasma $*: $JOBS compile jobs each"
mkdir -p build/plasma
for series in "$@"; do
    rm -f "build/plasma/$series.status"
    ( rc=0
      "$here/ci/forks.sh" --one "$series" 2>&1 || rc=$?
      echo "$rc" > "build/plasma/$series.status" ) | sed -u "s/^/[$series] /" &
done
wait
status=0
for series in "$@"; do
    if [ "$(cat "build/plasma/$series.status" 2>/dev/null)" = 0 ]; then
        echo "Plasma $series: done"
    else
        echo "Plasma $series: FAILED (its lines above, marked [$series])" >&2
        status=1
    fi
done
exit $status
