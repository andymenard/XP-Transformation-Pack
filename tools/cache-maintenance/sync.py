#!/usr/bin/env python3
from pathlib import Path
import shutil
ROOT=Path(__file__).resolve().parents[2]
for target in [ROOT/'installer/cache_maintenance.py', ROOT/'native/src/app/data/cache_maintenance.py']:
 shutil.copy2(Path(__file__).with_name('cleanup.py'),target)
