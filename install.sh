#!/bin/sh
# Install a build (./build.sh dev|release) onto this machine.
#
#   ./install.sh [dev|release] [PREFIX]      default: dev, ~/.local
#
# Copies build/<mode>/ into PREFIX -- bin/xplasma, bin/xplasma-edit, the style
# plugins under lib/xplasma/, and the Display Properties launcher, pointed at
# the installed editor. (Styling Qt apps is `xplasma apply`'s job: it copies
# the plugin to where the session's Qt looks.)
set -eu
mode="${1:-dev}"
prefix="${2:-$HOME/.local}"
root="$(cd "$(dirname "$0")" && pwd)"
from="$root/build/$mode"
[ -x "$from/bin/xplasma" ] || { echo "no build in $from: run ./build.sh $mode first" >&2; exit 1; }
mkdir -p "$prefix/bin" "$prefix/lib/xplasma" "$prefix/share/applications"
# (A fresh name, then a rename: a running editor keeps its old binary.)
for f in xplasma xplasma-edit; do
  cp "$from/bin/$f" "$prefix/bin/.$f.new" && mv -f "$prefix/bin/.$f.new" "$prefix/bin/$f"
done
for m in 6 5; do
  d="$prefix/lib/xplasma/qt$m/styles"
  mkdir -p "$d"
  cp "$from/lib/xplasma/qt$m/styles/libxplasma.so" "$d/.libxplasma.so.new" && mv -f "$d/.libxplasma.so.new" "$d/libxplasma.so"
done
desktop="$prefix/share/applications/fyi.hotsocket.xplasma.desktop"
sed "s|^Exec=xplasma-edit|Exec=$prefix/bin/xplasma-edit|" "$from/share/applications/fyi.hotsocket.xplasma.desktop" > "$desktop"
command -v update-desktop-database >/dev/null && update-desktop-database -q "$prefix/share/applications" || true
echo "installed $mode into $prefix"
