#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Cross-compile the XP-side tools: 32-bit, stock XP SP3, no C runtime
# (today's mingw-w64 links the UCRT, which XP doesn't have).
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
podman run --rm -v "$here:/src" -w /src localhost/xplasma-dev:3.8.1x sh -ec '
mkdir -p build
i686-w64-mingw32-windres uxdump.rc -O coff -o build/uxdump.res
i686-w64-mingw32-gcc -O2 -Wall -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns \
  -nostdlib -e _entry@0 -D_WIN32_WINNT=0x0501 -o build/uxdump.exe uxdump.c build/uxdump.res \
  -luxtheme -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -lshell32 -lkernel32 -lgcc \
  -Wl,--major-subsystem-version,5,--minor-subsystem-version,1,--major-os-version,5,--minor-os-version,1
i686-w64-mingw32-gcc -O2 -Wall -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns \
  -nostdlib -e _entry@0 -D_WIN32_WINNT=0x0501 -o build/xput.exe xput.c \
  -lws2_32 -luser32 -lshell32 -lkernel32 -lgcc \
  -Wl,--major-subsystem-version,5,--minor-subsystem-version,1,--major-os-version,5,--minor-os-version,1
i686-w64-mingw32-strip build/uxdump.exe build/xput.exe'
echo "built $here/build/uxdump.exe and xput.exe"
