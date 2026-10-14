#!/bin/sh
set -eu
suite_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$(id -u)" = 0 ]; then
 printf '%s\n' 'Launch the installer as your desktop user, not with sudo.' >&2
 exit 1
fi
if ! python3 -c 'import PyQt6.QtWidgets' 2>/dev/null && ! python3 -c 'import PySide6.QtWidgets' 2>/dev/null; then
 # The normal KDE authentication dialog installs only the GUI binding.
 pkexec /usr/bin/python3 "$suite_dir/root_helper.py" bootstrap
fi
exec python3 "$suite_dir/gui.py" "$@"
