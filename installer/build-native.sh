#!/bin/bash
set -euo pipefail
suite_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${1:?An installation build directory is required}
jobs=${XP_BUILD_JOBS:-2}
mkdir -p "$build_dir/source"
python3 - "$suite_dir" "$build_dir" <<'PY'
from pathlib import Path
import tarfile,sys,hashlib,shutil
root,build=map(Path,sys.argv[1:])
digest=hashlib.sha256((root/'source/xplasma-source.tar.gz').read_bytes()).hexdigest()
stamp=build/'source-sha256.txt'
if not stamp.exists() or stamp.read_text()!=digest:
 for name in ['source','native','guard']:
  if (build/name).exists():shutil.rmtree(build/name)
 (build/'source').mkdir(parents=True,exist_ok=True)
 with tarfile.open(root/'source/xplasma-source.tar.gz') as t:t.extractall(build/'source',filter='data')
 stamp.write_text(digest)
PY
source_dir="$build_dir/source/xplasma"
cmake -S "$source_dir/src/app/start-menu/popupguard" -B "$build_dir/guard" -G Ninja \
 -DCMAKE_BUILD_TYPE=Release -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$source_dir/src/app/start-menu/contents/ui/popupguard"
cmake --build "$build_dir/guard" --parallel "$jobs"
cmake -S "$source_dir" -B "$build_dir/native" -G Ninja \
 -DQT_MAJOR=6 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$build_dir/runtime"
cmake --build "$build_dir/native" --parallel "$jobs" --target xplasma-cli xplasma-edit xplasma-setup xplasma test-style-geometry
QT_QPA_PLATFORM=offscreen QT_STYLE_OVERRIDE=Windows "$build_dir/native/tests/style/test-style-geometry"
cmake --install "$build_dir/native"
if [ "${2:-core}" = taskbar ]; then
 python3 "$suite_dir/build-widgets.py" "$build_dir" "$jobs"
fi
