#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Boot the XP reference VM for the pixel oracle: ~/clod/xpvm/xp.sh plus
#   * a QMP control socket (refharness/vm.py drives it: screenshots, keys),
#   * -snapshot: nothing a test run does touches xp.qcow2; it is all thrown
#     away at power-off. XPVM_PERSIST=1 boots without it, for when the VM
#     itself should change.
# Jobs go in on an ISO swapped into the CD drive over QMP, and results come
# back over HTTP (xput.exe -> 10.0.2.2); refharness/oracle.py does both. (A
# read-write FAT drive was tried first: QEMU's vvfat crashed with "cluster 0
# used more than once" on the guest's first write.)
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
run="${XDG_RUNTIME_DIR:-/tmp}/xplasma-vm"
mkdir -p "$run"
rm -f "$run/qmp.sock"
snapshot="-snapshot"
[ "${XPVM_PERSIST:-0}" = 1 ] && snapshot=""
exec "${XPVM:-$HOME/clod/xpvm/xp.sh}" $snapshot \
    -qmp "unix:$run/qmp.sock,server=on,wait=off" \
    "$@"
