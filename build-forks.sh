#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# XPlasma's builds of Plasma's own widgets, one per Plasma series (see
# src/app/forks.h): the tray, notifications, task manager and panel that Plasma
# 6.5+ builds into its plugins, patched as XPlasma patches the packaged ones.
#
#   ./build-forks.sh SERIES...        e.g. ./build-forks.sh 6.6
#   JOBS=4 ./build-forks.sh 6.8       compiling at most 4 at a time
#
# For each series, under build/plasma/SERIES/:
#   src/       the pinned upstream tarballs, downloaded once, checked against
#              their SHA-256 (src/app/overrides/forks.json) every run
#   pristine/  unpacked once
#   work/      the patched source: xplasma fork-sources writes only the
#              files whose content changed, so the build is incremental
#   build/     the build trees, kept (configured once; ninja rebuilds what
#              changed, ccache/ the rest)
#   out/plasma/applets/  the plugins -- checked here to use no Qt private
#              API and to need no Qt newer than the series' own floor
#
# Needs podman (CONTAINER=docker for Docker), the series' image
# (containers/plasma-SERIES.Containerfile), and the dev build's xplasma --
# brought up to date first (./build.sh dev, incremental), since the patches
# it applies are built into it. XPLASMA names another xplasma to use instead
# (CI: the release build's, in its container -- ci/xplasma.sh), and then no
# dev build is made. FORKS_PREPARE_ONLY=1 stops at the patched source (CI
# works out from it whether a stored build will do).
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
# Compiling Plasma takes about a gigabyte a job: as many as this machine's
# memory allows, never more than its processors (JOBS overrides).
jobs="${JOBS:-$(awk '/MemAvailable/ { print int($2 / 1100000) }' /proc/meminfo)}"
[ "${jobs:-0}" -ge 1 ] || jobs=1
[ "$jobs" -le "$(nproc)" ] || jobs=$(nproc)
container="${CONTAINER:-podman}"
xplasma="${XPLASMA:-$here/build/dev/bin/xplasma}"
[ $# -gt 0 ] || { echo "usage: $0 SERIES..." >&2; exit 2; }
if [ -z "${XPLASMA:-}" ]; then
    "$here/build.sh" dev >/dev/null || { echo "build-forks: the dev build failed (./build.sh dev)" >&2; exit 1; }
fi

for series in "$@"; do
    root="$here/build/plasma/$series"
    info="$("$xplasma" fork-info "$series")"
    image="$(printf '%s\n' "$info" | awk '$1 == "image" { print $2 }')"
    floor="$(printf '%s\n' "$info" | awk '$1 == "qtfloor" { print $2 }')"
    mkdir -p "$root/src" "$root/pristine" "$root/work" "$root/build" "$root/ccache" "$root/out/plasma/applets"

    # The sources: fetched once, checked every time, unpacked once.
    printf '%s\n' "$info" | awk '$1 == "source" { print $2, $3, $4 }' | while read -r project version sha; do
        tarball="$root/src/$project-$version.tar.xz"
        if [ ! -f "$tarball" ]; then
            echo "fetching $project $version"
            # (KDE's betas, x.y.90 and up, are under unstable/)
            case "$version" in *.9[0-9]) where=unstable ;; *) where=stable ;; esac
            curl -fsSL -o "$tarball.part" "https://download.kde.org/$where/plasma/$version/$project-$version.tar.xz"
            mv "$tarball.part" "$tarball"
        fi
        echo "$sha  $tarball" | sha256sum -c --quiet - || { echo "build-forks: $tarball doesn't match its checksum" >&2; exit 1; }
        [ -d "$root/pristine/$project-$version" ] || tar xJf "$tarball" -C "$root/pristine"
    done

    # The patched source.
    "$xplasma" fork-sources "$series" "$root/pristine" "$root/work"
    [ "${FORKS_PREPARE_ONLY:-0}" = 1 ] && continue

    # Only XPlasma's targets, per project.
    for project in $(printf '%s\n' "$info" | awk '$1 == "target" { print $2 }' | sort -u); do
        version="$(printf '%s\n' "$info" | awk -v p="$project" '$1 == "source" && $2 == p { print $3 }')"
        targets="$(printf '%s\n' "$info" | awk -v p="$project" '$1 == "target" && $2 == p { print $3 }' | tr '\n' ' ')"
        echo "building $project $version: $targets"
        "$container" run --rm -v "$root":/w:z -w /w -e CCACHE_DIR=/w/ccache "$image" sh -c "
            set -e
            if [ ! -f build/$project/build.ninja ]; then
                cmake -S work/$project-$version -B build/$project -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
                      -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache > build/$project.configure.log 2>&1 \
                    || { tail -30 build/$project.configure.log; exit 1; }
            fi
            cmake --build build/$project --target $targets -- -j$jobs"
        for target in $targets; do
            cp -p "$root/build/$project/bin/plasma/applets/$target.so" "$root/out/plasma/applets/"
        done
    done

    # The plugins: public Qt API only, and nothing past the series' floor.
    status=0
    for so in "$root"/out/plasma/applets/*.so; do
        symbols="$(objdump -T "$so")"
        if printf '%s\n' "$symbols" | grep -q 'PRIVATE_API'; then
            echo "FAIL $(basename "$so"): uses Qt's private API" >&2; status=1
        fi
        newest="$(printf '%s\n' "$symbols" | grep -o '(Qt_6\.[0-9]*)' | tr -d '()' | sed 's/Qt_//' | sort -V | tail -1)"
        if [ -n "$newest" ] && [ "$(printf '%s\n%s\n' "$newest" "$floor" | sort -V | tail -1)" != "$floor" ]; then
            echo "FAIL $(basename "$so"): needs Qt $newest (floor $floor)" >&2; status=1
        fi
        echo "ok   $(basename "$so")  (Qt floor ${newest:-6.0} <= $floor)"
    done
    [ $status -eq 0 ] || exit $status
done
