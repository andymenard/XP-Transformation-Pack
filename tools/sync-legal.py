#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synchronize release legal documents; --check detects stale embedded copies."""
import argparse, shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sync(check=False):
 pairs=[(ROOT/'CREDITS.md',ROOT/'native/CREDITS.md'),
        (ROOT/'CREDITS.md',ROOT/'installer/CREDITS.md'),
        (ROOT/'native/LICENSE',ROOT/'installer/LICENSE'),
        (ROOT/'native/THIRD-PARTY-NOTICES',ROOT/'installer/THIRD-PARTY-NOTICES')]
 pairs += [(p,ROOT/'installer/LICENSES'/p.name) for p in sorted((ROOT/'native/LICENSES').glob('*.txt'))]
 stale=[]
 for source,target in pairs:
  if not target.is_file() or source.read_bytes()!=target.read_bytes():
   if check:stale.append(str(target.relative_to(ROOT)))
   else:target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
 if stale:raise SystemExit('Stale legal documents; run python3 tools/sync-legal.py: '+', '.join(stale))
if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true')
 sync(parser.parse_args().check)
