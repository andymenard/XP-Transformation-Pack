# Build image for XPLasma's forks of Plasma's compiled widgets for the
# Plasma 6.8 series, whose libPlasma is .so.8 (see plasma-6.6.Containerfile
# for the why of it all).
#
# Until distributions carry 6.8, its development files come from Arch
# Linux's kde-unstable repository (Plasma's betas, 6.7.9x), on the Qt it
# brings (6.12's release candidates): this series' floor is Qt 6.12 until
# it's built again on a distribution's 6.8.
#
#   podman build -t xplasma-plasma:6.8 -f containers/plasma-6.8.Containerfile containers
FROM docker.io/library/archlinux:latest
RUN sed -i 's/^\[core\]$/[kde-unstable]\nInclude = \/etc\/pacman.d\/mirrorlist\n\n[core]/' /etc/pacman.conf \
 && pacman -Syu --noconfirm --needed base-devel cmake ninja ccache git xz python extra-cmake-modules kdoctools \
      plasma-workspace plasma-desktop plasma-wayland-protocols wayland-protocols qt6-tools qt6-declarative qt6-wayland \
      qt6-5compat qt6-svg kunitconversion krunner kholidays ktexteditor kuserfeedback appstream-qt phonon-qt6 \
      libqalculate networkmanager-qt prison kpipewire kscreenlocker libkexiv2 baloo kaccounts-integration \
      packagekit-qt6 libxkbfile xorg-server-devel libxcursor libxtst sdl2 xf86-input-libinput xf86-input-evdev \
      xdotool kactivitymanagerd plasma-activities-stats intltool \
 && pacman -Scc --noconfirm \
 && pacman -Q plasma-workspace qt6-base
ENV CCACHE_DIR=/ccache
