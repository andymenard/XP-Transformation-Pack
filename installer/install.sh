#!/bin/sh
set -eu
suite_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$(id -u)" = 0 ]; then
 printf '%s\n' 'Launch the installer as your desktop user, not with sudo.' >&2
 exit 1
fi
# A release folder lists every file it contains in SHA256SUMS.json. If the
# archive was not fully extracted, say so before the installer's own modules
# fail to import (the backend checks the checksums themselves later).
missing=
if [ -f "$suite_dir/SHA256SUMS.json" ]; then
 missing=$(python3 -I - "$suite_dir" <<'EOF'
import json,sys
from pathlib import Path
base=Path(sys.argv[1])
try:names=list(json.loads((base/'SHA256SUMS.json').read_text()))
except (OSError,ValueError):sys.exit(print('its file list SHA256SUMS.json is damaged'))
gone=[n for n in names if not ((base/n).is_symlink() or (base/n).exists())]
if gone:print(str(len(gone))+' of '+str(len(names))+' files are missing, including '+gone[0])
EOF
) || missing='its file list could not be read'
elif [ -f "$suite_dir/BUILD-PROVENANCE.json" ]; then
 missing='its file list SHA256SUMS.json is missing'
fi
if [ -n "$missing" ]; then
 message="This installer folder is incomplete: $missing. Delete the folder, extract the archive again, and wait until extraction has finished."
 kdialog --title 'XPlasma Plus' --error "$message" 2>/dev/null || zenity --error --title='XPlasma Plus' --text="$message" 2>/dev/null || :
 printf '%s\n' "$message" >&2
 exit 1
fi
if ! python3 -c 'import PyQt6.QtWidgets' 2>/dev/null && ! python3 -c 'import PySide6.QtWidgets' 2>/dev/null; then
 # The normal KDE authentication dialog installs only the GUI binding.
 pkexec /usr/bin/python3 "$suite_dir/root_helper.py" bootstrap
fi
exec python3 "$suite_dir/gui.py" "$@"
