#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Install a build (./build.sh dev|release) onto this machine.
#
#   ./install.sh [dev|release] [PREFIX]      default: dev, ~/.local
#
# Copies build/<mode>/ into PREFIX -- bin/xplasma, bin/xplasma-edit, the style
# plugins and the builds of Plasma's widgets under lib/xplasma/, and the
# desktop entries, pointed at the installed editor. (Styling Qt apps is `xplasma apply`'s job: it copies
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
# XPlasma's builds of Plasma's widgets, per Plasma series (apply picks one).
if [ -d "$from/lib/xplasma/forks" ]; then
  rm -rf "$prefix/lib/xplasma/forks.new"
  cp -r "$from/lib/xplasma/forks" "$prefix/lib/xplasma/forks.new"
  rm -rf "$prefix/lib/xplasma/forks" && mv "$prefix/lib/xplasma/forks.new" "$prefix/lib/xplasma/forks"
fi
# The desktop entries (XPlasma Properties, Date and Time), pointed at the
# installed editor.
rm -f "$prefix/share/applications/fyi.hotsocket.xplasma.sounds.desktop"   # (retired: now a tab)
for desktop in "$from"/share/applications/*.desktop; do
  sed "s|^Exec=xplasma-edit|Exec=$prefix/bin/xplasma-edit|" "$desktop" > "$prefix/share/applications/${desktop##*/}"
done
command -v update-desktop-database >/dev/null && update-desktop-database -q "$prefix/share/applications" || true
# What runs at login: the update check (XP's globe when a newer XPlasma is out).
mkdir -p "$HOME/.config/autostart"
for desktop in "$from"/share/xplasma/autostart/*.desktop; do
  [ -f "$desktop" ] || continue
  sed "s|^Exec=xplasma-edit|Exec=$prefix/bin/xplasma-edit|" "$desktop" > "$HOME/.config/autostart/${desktop##*/}"
done
# The licence and the third-party notices.
if [ -d "$from/share/licenses/xplasma" ]; then
  mkdir -p "$prefix/share/licenses/xplasma"
  cp "$from"/share/licenses/xplasma/* "$prefix/share/licenses/xplasma/"
fi
echo "installed $mode into $prefix"
