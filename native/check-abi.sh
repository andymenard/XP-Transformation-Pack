#!/bin/sh
# Release gate for a built style plugin: is it really portable?
#
#   native/check-abi.sh path/to/libxplasma.so [max-glibc]
#
# A plugin that passes loads into every Qt of its major at or above its build
# floor, on any distribution with at least `max-glibc` (default 2.34, the same
# floor xp2plasma's AppImage measured). Checks:
#   1. Qt symbols use only the public Qt_N / Qt_N.M versions (no *_PRIVATE_API).
#   2. No C++-runtime or other non-Qt symbol is bound to a Qt version.
#   3. Only Qt's two plugin entry points are exported.
#   4. No glibc symbol newer than max-glibc.
set -eu
lib="$1"; max="${2:-2.34}"
fail=0
say() { printf '%s\n' "$*"; }
dyn="$(objdump -T "$lib")"

private="$(printf '%s\n' "$dyn" | grep -o '(Qt_[^)]*PRIVATE[^)]*)' | sort -u || true)"
[ -z "$private" ] || { say "FAIL private Qt API: $private"; fail=1; }

stray="$(printf '%s\n' "$dyn" | awk '/\(Qt_/ {print $NF}' | c++filt | grep -v '^\(typeinfo for \|vtable for \)\?[Qq]' || true)"
[ -z "$stray" ] || { say "FAIL non-Qt symbols bound to Qt:"; say "$stray"; fail=1; }

# (__bss_start, _edata and _end are the linker's own section markers.)
exports="$(printf '%s\n' "$dyn" | awk '$2 == "g" && $4 != "*UND*" {print $NF}' \
           | grep -v '^\(__bss_start\|_edata\|_end\)$' | sort -u | tr '\n' ' ')"
case "$exports" in
  "qt_plugin_instance qt_plugin_query_metadata "|"qt_plugin_instance qt_plugin_query_metadata_v2 ") ;;
  *) say "FAIL unexpected exports: $exports"; fail=1 ;;
esac

newest="$(printf '%s\n' "$dyn" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -V | tail -1)"
if [ -n "$newest" ] && [ "$(printf '%s\n%s\n' "$newest" "$max" | sort -V | tail -1)" != "$max" ]; then
  say "FAIL needs glibc $newest (floor $max)"; fail=1
fi

floor="$(printf '%s\n' "$dyn" | grep -o '(Qt_[0-9]*\.[0-9]*)' | sort -u | tr -d '()' | tr '\n' ' ')"
[ "$fail" = 0 ] && say "ok   $lib  (Qt floor: ${floor:-?}, glibc ≤ ${newest:-?})"
exit "$fail"
