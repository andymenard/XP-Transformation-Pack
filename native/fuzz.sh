#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Fuzz the core's parsers: PE/theme, DIB, INI and .theme/scheme, each under
# ASan + UBSan.
#
#   ./fuzz.sh [SECONDS]      default 600 per target, all in parallel
#
# Seeds come from tests/oracle/synth.py (always) plus, when the corpus is on
# this machine, real resources extracted from it (seeds stay local: they are
# derived from Microsoft's files); the scheme target starts from XP's stock
# schemes in src/app/data/schemes. Crashes land in build/fuzz/crashes/.
set -eu
root="$(cd "$(dirname "$0")" && pwd)"
seconds="${1:-600}"
work="$root/build/fuzz"
mkdir -p "$work/seeds/theme" "$work/seeds/dib" "$work/seeds/ini" "$work/seeds/scheme" "$work/seeds/icon" "$work/crashes"
"$root/.venv/bin/python3" "$root/tests/oracle/fuzz_seeds.py" "$work/seeds"
cp "$root"/src/app/data/schemes/*.theme "$work/seeds/scheme/"
# (icon: the theme seeds' PE files carry no icons; libFuzzer builds them up
# from the DIB seeds.)
cp "$work"/seeds/dib/* "$work/seeds/icon/" 2>/dev/null || true
podman run --rm -v "$root:/src" -w /src localhost/xplasma-dev:3.8.1x sh -ec '
cmake -S . -B build/fuzz/build -G Ninja -DQT_MAJOR=6 -DXPLASMA_FUZZ=ON \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined" >/dev/null
cmake --build build/fuzz/build --target fuzz-theme fuzz-dib fuzz-ini fuzz-scheme fuzz-icon >/dev/null'
podman run --rm -v "$root:/src" -w /src/build/fuzz localhost/xplasma-dev:3.8.1x sh -c "
for t in theme dib ini scheme icon; do
  ( mkdir -p corpus/\$t && ./build/fuzz-\$t corpus/\$t seeds/\$t -max_total_time=$seconds -max_len=4194304 \
      -rss_limit_mb=4096 -timeout=10 -artifact_prefix=crashes/\$t- -print_final_stats=1 > log-\$t.txt 2>&1;
    echo \"\$t exit \$?\" ) &
done
wait"
for t in theme dib ini scheme icon; do
  printf '%-6s ' "$t"; grep -E "stat::number_of_executed_units|ERROR|SUMMARY" "$work/log-$t.txt" | head -2 | tr '\n' ' '; echo
done
ls "$work/crashes"
