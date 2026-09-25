# Build image for XPLasma's forks of Plasma's compiled widgets for the
# Plasma 6.7 series, whose libPlasma is .so.7 (see plasma-6.6.Containerfile
# for the why of it all).
#
# Fedora 44's updates carry Plasma 6.7 with its development files -- and
# Qt 6.11 beside it, so this series' floor is Qt 6.11, not Plasma 6.7's own
# minimum of 6.10: no distribution image carries 6.7 on 6.10 to build
# against. A Plasma 6.7 on Qt 6.10 finds this build won't load, and
# XPLasma says so rather than applying half a look.
#
#   podman build -t xplasma-plasma:6.7 -f containers/plasma-6.7.Containerfile containers
FROM registry.fedoraproject.org/fedora:44
RUN dnf -y --setopt=tsflags=noscripts install 'dnf5-command(builddep)' ccache ninja-build cmake gcc-c++ git xz python3 \
 && dnf -y --setopt=tsflags=noscripts builddep plasma-workspace plasma-desktop \
 && dnf clean all \
 && rpm -q qt6-qtbase-devel libplasma-devel
ENV CCACHE_DIR=/ccache
