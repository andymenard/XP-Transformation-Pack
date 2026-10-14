#!/bin/sh
set -eu
cd -- "$(dirname -- "$0")"
if [ "$(id -u)" = 0 ]; then echo 'Run as your desktop user, without sudo.' >&2; exit 1; fi
if ! command -v openssl >/dev/null || ! command -v gh >/dev/null; then
 if command -v apt-get >/dev/null; then pkexec apt-get install -y openssl gh
 elif command -v dnf >/dev/null; then pkexec dnf install -y openssl gh
 elif command -v pacman >/dev/null; then pkexec pacman -S --needed --noconfirm openssl github-cli
 elif command -v zypper >/dev/null; then pkexec zypper --non-interactive install openssl gh
 else echo 'Install openssl and GitHub CLI (gh), then rerun.' >&2; exit 1; fi
fi
if ! python3 -c 'import PyQt6.QtWidgets' 2>/dev/null && ! python3 -c 'import PySide6.QtWidgets' 2>/dev/null; then
 echo 'Qt Python bindings are missing. Install python3-pyqt6 (or PySide6) from your distribution, then rerun.' >&2; exit 1
fi
python3 install.py
exec python3 "$HOME/.local/share/xplasma-updater/app/gui.py"
