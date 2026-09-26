#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# XPlasma-Setup: the one file to download. The Setup wizard (xplasma-setup)
# with XPlasma appended to it -- a tar of the release tree's programs, style
# plugins, builds of Plasma's widgets and application menu entries -- then
# the tar's offset (8 bytes, little-endian) and "XPLSETUP". The wizard finds
# it there and unpacks it (src/app/wizard/main.cpp).
#
#   ./package.sh [release|dev]      writes build/XPlasma-Setup
#
# Build first (./build.sh release, and ./build-forks.sh for Plasma's widgets).
set -eu
mode="${1:-release}"
root="$(cd "$(dirname "$0")" && pwd)"
from="$root/build/$mode"
out="$root/build/XPlasma-Setup"
for f in bin/xplasma bin/xplasma-edit bin/xplasma-setup; do
  [ -x "$from/$f" ] || { echo "no $f in $from: run ./build.sh $mode first" >&2; exit 1; }
done
tar="$(mktemp)"
trap 'rm -f "$tar" "$out.new"' EXIT
tar -C "$from" -cf "$tar" bin/xplasma bin/xplasma-edit lib share
cp "$from/bin/xplasma-setup" "$out.new"
offset=$(stat -c %s "$out.new")
cat "$tar" >> "$out.new"
# The offset, a byte at a time, lowest first.
n=$offset
i=0
while [ $i -lt 8 ]; do
  printf "\\$(printf %03o $((n % 256)))" >> "$out.new"
  n=$((n / 256))
  i=$((i + 1))
done
printf 'XPLSETUP' >> "$out.new"
chmod +x "$out.new"
mv "$out.new" "$out"
echo "$out ($(du -h "$out" | cut -f1)), XPlasma from $from"
