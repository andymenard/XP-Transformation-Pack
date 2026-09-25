# XPlasma release image: builds the portable plugins and the app.
#
# A plugin built against Qt 6.N loads into every Qt >= 6.N (public API only,
# see PLAN.md D1), so the release build uses the *oldest* Qt each major
# supports: 6.6 (Plasma 6.0's own minimum) and 5.15. Both are the Qt Company's
# official binaries, fetched with aqtinstall.
#
# Debian 12 sets the other floors: glibc 2.36 and GCC 12's libstdc++, both
# older than any distribution that ships Plasma 6.
#
#   podman build -t xplasma-release:2 -f containers/release.Containerfile containers
FROM docker.io/library/debian:12
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build python3 python3-venv ca-certificates \
      libgl-dev libegl-dev libxkbcommon-dev libfontconfig1-dev libfreetype-dev libdbus-1-dev libglib2.0-0 \
      zlib1g-dev libbz2-dev liblzma-dev \
 && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/aqt && /opt/aqt/bin/pip install -q aqtinstall \
 && /opt/aqt/bin/aqt install-qt -O /opt/qt linux desktop 6.6.3 gcc_64 --archives qtbase icu \
 && /opt/aqt/bin/aqt install-qt -O /opt/qt linux desktop 5.15.2 gcc_64 --archives qtbase icu \
 && rm -rf /root/.cache
ENV QT6_DIR=/opt/qt/6.6.3/gcc_64 QT5_DIR=/opt/qt/5.15.2/gcc_64
