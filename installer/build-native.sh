#!/bin/bash
set -euo pipefail
suite_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${1:?An installation build directory is required}
jobs=$(python3 "$suite_dir/build_cache.py" jobs)
kind=${2:-core}
mkdir -p "$build_dir/source"
python3 - "$suite_dir" "$build_dir" <<'PY'
from pathlib import Path
import tarfile,sys,shutil
root,build=map(Path,sys.argv[1:])
sys.path.insert(0,str(root));from build_cache import source_id
digest=source_id(root)
stamp=build/'source-sha256.txt'
if not stamp.exists() or stamp.read_text()!=digest:
 for name in ['source']:
  if (build/name).exists():shutil.rmtree(build/name)
 (build/'source').mkdir(parents=True,exist_ok=True)
 with tarfile.open(root/'source/xplasma-source.tar.gz') as t:t.extractall(build/'source',filter='data')
 stamp.write_text(digest)
PY
source_dir="$build_dir/source/xplasma"
if python3 "$suite_dir/build_cache.py" reuse "$suite_dir" "$build_dir" "$kind"; then
 exit 0
fi
cmake -S "$source_dir" -B "$build_dir/native" -G Ninja \
 -DQT_MAJOR=6 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$build_dir/runtime"
cmake --build "$build_dir/native" --parallel "$jobs" --target xplasma-cli xplasma-edit xplasma-setup xplasma test-style-geometry
QT_QPA_PLATFORM=offscreen QT_STYLE_OVERRIDE=Windows "$build_dir/native/tests/style/test-style-geometry"
cmake --install "$build_dir/native"
if [ "${2:-core}" = taskbar ]; then
 python3 "$suite_dir/build-widgets.py" "$build_dir" "$jobs"
fi
python3 "$suite_dir/build_cache.py" store "$suite_dir" "$build_dir" "$kind"
