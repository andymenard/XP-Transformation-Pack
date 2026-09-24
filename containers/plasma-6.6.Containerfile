# Build image for XPLasma's forks of Plasma's compiled widgets (the tray,
# the task manager, notifications) for one Plasma series: here 6.6, whose
# libPlasma is .so.7 -- from Plasma 6.6 on, each release bumps it, so each
# series gets its own image, its own build, and XPLasma installs the one
# matching the running Plasma.
#
# Fedora 44 ships Plasma 6.6 with its development files, so building its
# plasma-workspace/plasma-desktop sources here links against exactly the
# libraries those Plasmas carry -- from Fedora's release repositories only,
# never its updates: they carry Qt 6.10, Plasma 6.6's own minimum, and a
# plugin built against Qt 6.N loads into Qt 6.N and later only. (The QML is
# kept as source, not compiled ahead to C++, whose code calls Qt's private
# API and would tie a build to one exact Qt.) The source and build trees
# live outside, mounted in (plasma/build.sh), so builds are incremental.
# (Package scripts are skipped: mixing the release repositories with the
# base image's updated libraries breaks some, and nothing here needs them.)
#
#   podman build -t xplasma-plasma:6.6 -f containers/plasma-6.6.Containerfile containers
FROM registry.fedoraproject.org/fedora:44
RUN dnf -y --disablerepo=updates --setopt=tsflags=noscripts install 'dnf5-command(builddep)' ccache ninja-build cmake gcc-c++ git xz python3 \
 && dnf -y --disablerepo=updates --setopt=tsflags=noscripts builddep plasma-workspace plasma-desktop \
 && dnf clean all \
 && rpm -q qt6-qtbase-devel libplasma-devel
ENV CCACHE_DIR=/ccache
