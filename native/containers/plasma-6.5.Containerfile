# SPDX-License-Identifier: GPL-3.0-or-later
# Plasma 6.5 compiles its panel and notifications. Its task manager and tray
# are still QML packages, patched in the user's data directory.
FROM registry.fedoraproject.org/fedora:43
RUN dnf -y --disablerepo=updates --setopt=tsflags=noscripts install 'dnf5-command(builddep)' ccache ninja-build cmake gcc-c++ git xz python3 \
 && dnf -y --disablerepo=updates --setopt=tsflags=noscripts builddep plasma-workspace plasma-desktop \
 && dnf clean all \
 && rpm -q qt6-qtbase-devel libplasma-devel
ENV CCACHE_DIR=/ccache
