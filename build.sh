#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Build XPlasma -- the app, its editor and the style plugins -- and install it
# into build/<mode>/ (bin/, lib/xplasma/qt{6,5}/styles/, share/applications/).
#
#   ./build.sh dev       host-matched toolchain and Qt; fastest iteration
#   ./build.sh release   oldest supported Qt (6.6.3 / 5.15.2); portable,
#                        and gated by check-abi.sh
#
# Both run in podman (the containers/ images), so the host needs no compiler.
# `dev` output loads only into Qt >= the host's own; `release` output is what
# ships, and loads into every Qt >= 6.6 / 5.15. install.sh copies either onto
# this machine.
set -eu
mode="${1:-dev}"
root="$(cd "$(dirname "$0")" && pwd)"
case "$mode" in
  dev)     image=localhost/xplasma-dev:3.8.1x; prefix6=; prefix5= ;;
  release) image=localhost/xplasma-release:2;  prefix6='$QT6_DIR'; prefix5='$QT5_DIR' ;;
  *) echo "usage: $0 dev|release" >&2; exit 2 ;;
esac
podman run --rm -v "$root:/src" -w /src "$image" sh -ec "
for m in 6 5; do
  if [ \$m = 6 ]; then qt=$prefix6; else qt=$prefix5; fi
  cmake -S . -B build/$mode-qt\$m -G Ninja -DQT_MAJOR=\$m \
        -DCMAKE_BUILD_TYPE=$( [ "$mode" = release ] && echo Release || echo RelWithDebInfo ) \
        \${qt:+-DCMAKE_PREFIX_PATH=\$qt} -DCMAKE_INSTALL_PREFIX=/src/build/$mode >/dev/null
  cmake --build build/$mode-qt\$m
  cmake --install build/$mode-qt\$m >/dev/null
done"
# Plasma's widgets as XPlasma builds them (./build-forks.sh), per Plasma series.
rm -rf "$root/build/$mode/lib/xplasma/forks"
for out in "$root"/build/plasma/*/out; do
  [ -d "$out/plasma/applets" ] || continue
  series="$(basename "$(dirname "$out")")"
  mkdir -p "$root/build/$mode/lib/xplasma/forks/$series"
  cp -a "$out/plasma" "$root/build/$mode/lib/xplasma/forks/$series/"
done
if [ "$mode" = release ]; then
  for m in 6 5; do "$root/check-abi.sh" "$root/build/$mode/lib/xplasma/qt$m/styles/libxplasma.so"; done
fi
echo "installed into build/$mode"
