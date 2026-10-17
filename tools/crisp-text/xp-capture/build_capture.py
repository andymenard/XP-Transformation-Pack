#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cross-compile the XP-only capture utility using an existing Zig 0.13 toolchain."""
import argparse,json,subprocess
from pathlib import Path

def build(compiler,output):
 env=json.loads(subprocess.check_output([str(compiler),'env'],text=True))
 includes=Path(env['lib_dir'])/'libc/include/any-windows-any'
 output=Path(output).resolve();output.parent.mkdir(parents=True,exist_ok=True)
 subprocess.run([str(compiler),'cc','-target','x86-windows-gnu','-isystem',str(includes),'-Os','-ffreestanding','-fno-stack-protector','-nostdlib','-Wl,--entry,mainCRTStartup@0','-Wl,--subsystem,windows','-Wl,--major-subsystem-version,5','-Wl,--minor-subsystem-version,1','-Wl,--major-os-version,5','-Wl,--minor-os-version,1',str(Path(__file__).with_name('capture.c')),'-lkernel32','-lgdi32','-luser32','-o',str(output)],check=True)
 print(output)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--zig',default='zig');p.add_argument('output',type=Path);a=p.parse_args();build(a.zig,a.output)
