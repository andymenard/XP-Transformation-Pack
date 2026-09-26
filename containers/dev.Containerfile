# SPDX-License-Identifier: GPL-3.0-or-later
# XPlasma dev image: the host's own toolchain and Qt, for fast iteration.
#
# Based on xp2plasma's kvantum build image, which is Arch re-pointed at only
# Valve's core-/extra-<release> repos -- so gcc, glibc and Qt 6 match this
# SteamOS release exactly and anything built here loads into host apps.
# Adds Qt 5 (the second plugin build), ninja, and mingw-w64 for the XP-side
# reference harness (refharness/), plus clang + compiler-rt for libFuzzer.
#
#   podman build -t xplasma-dev:3.8.1x -f containers/dev.Containerfile containers
ARG BASE=localhost/xp2p-kvantum-build:3.8.1x
FROM ${BASE}
RUN pacman -S --noconfirm --needed --overwrite '*' qt5-base ninja mingw-w64-gcc gdb clang compiler-rt \
 && pacman -Scc --noconfirm
