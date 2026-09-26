#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Drive the XP reference VM over QMP: screenshots, keystrokes, Run dialog.

    vm.py screendump out.png     what the VM's screen shows right now
    vm.py type 'E:\\uxdump.exe'   type text (US layout, as XP is set up)
    vm.py key meta_l-r           press a key combination (QEMU qcodes)
    vm.py run 'E:\\go.cmd'        Win+R, type, Enter
    vm.py powerdown              ACPI power button (XP shuts down cleanly)
    vm.py wait                   until XP has booted to a settled desktop

QMP serves one client at a time, so hold one connection per process (pass it
around) and close it when done; a second connect blocks until the first closes.

The socket is the one refharness/vm.sh creates. Standard library only (plus
Pillow for PNG output), so it runs from the app's venv or the system Python.
"""
from __future__ import annotations

import json
import os
import socket
import sys
import tempfile
import time

SOCKET = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "xplasma-vm", "qmp.sock")

# US-layout text -> QEMU qcodes. Shifted characters carry "shift-".
_PLAIN = {c: c for c in "abcdefghijklmnopqrstuvwxyz0123456789"}
_PLAIN.update({" ": "spc", "\\": "backslash", "/": "slash", ".": "dot", ",": "comma", "-": "minus",
               "=": "equal", ";": "semicolon", "'": "apostrophe", "[": "bracket_left",
               "]": "bracket_right", "`": "grave_accent", "\n": "ret", "\t": "tab"})
_SHIFTED = {":": "semicolon", "_": "minus", '"': "apostrophe", "!": "1", "@": "2", "#": "3", "$": "4",
            "%": "5", "^": "6", "&": "7", "*": "8", "(": "9", ")": "0", "+": "equal", "?": "slash",
            "<": "comma", ">": "dot", "{": "bracket_left", "}": "bracket_right", "|": "backslash",
            "~": "grave_accent"}


class QMP:
    def __init__(self, path: str = SOCKET) -> None:
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        # QMP serves one client at a time: a hung client blocks every other.
        # Nothing here may wait forever.
        self.sock.settimeout(30)
        self.sock.connect(path)
        self.file = self.sock.makefile("rwb")
        self._read()                                  # greeting
        self.command("qmp_capabilities")

    def close(self) -> None:
        self.file.close()
        self.sock.close()

    def __enter__(self) -> "QMP":
        return self

    def __exit__(self, *_exc) -> None:
        self.close()

    def _read(self) -> dict:
        while True:
            msg = json.loads(self.file.readline())
            if "event" not in msg:                    # events arrive interleaved; skip them
                return msg

    def command(self, name: str, **args) -> dict:
        self.file.write(json.dumps({"execute": name, "arguments": args}).encode() + b"\n")
        self.file.flush()
        reply = self._read()
        if "error" in reply:
            raise RuntimeError(f"{name}: {reply['error']}")
        return reply.get("return", {})

    def keys(self, *qcodes: str, hold_ms: int = 60) -> None:
        self.command("send-key", keys=[{"type": "qcode", "data": q} for q in qcodes], **{"hold-time": hold_ms})

    def type(self, text: str) -> None:
        for ch in text:
            if ch in _PLAIN:
                self.keys(_PLAIN[ch])
            elif ch.lower() in _PLAIN and ch.isupper():
                self.keys("shift", _PLAIN[ch.lower()])
            elif ch in _SHIFTED:
                self.keys("shift", _SHIFTED[ch])
            else:
                raise ValueError(f"can't type {ch!r}")
            time.sleep(0.02)

    def run(self, command: str) -> None:
        """Start a program through XP's Run dialog."""
        self.keys("meta_l", "r")
        time.sleep(1.0)
        self.type(command)
        time.sleep(0.2)
        self.keys("ret")

    def screendump(self, png: str) -> str:
        from PIL import Image
        with tempfile.TemporaryDirectory() as tmp:
            ppm = os.path.join(tmp, "screen.ppm")
            self.command("screendump", filename=ppm)
            Image.open(ppm).save(png)
        return png


def wait_for_desktop(vm: QMP, timeout: float = 180.0, scratch: str | None = None) -> float:
    """Block until XP's screen is lit and has stopped changing (the desktop
    after boot). Returns the seconds it took."""
    import hashlib
    from PIL import Image
    start, last, stable = time.time(), None, 0
    with tempfile.TemporaryDirectory() as tmp:
        shot = os.path.join(scratch or tmp, "boot.png")
        while time.time() - start < timeout:
            time.sleep(3)
            image = Image.open(vm.screendump(shot)).convert("L")
            digest = hashlib.md5(image.tobytes()).hexdigest()
            lit = sum(image.resize((32, 24)).tobytes()) / 768
            stable = stable + 1 if digest == last else 0
            last = digest
            if lit > 40 and stable >= 3:
                return time.time() - start
    raise TimeoutError(f"no settled desktop after {timeout:.0f}s")


def connect(timeout: float = 20.0) -> QMP:
    """Connect once the VM's socket exists (vm.sh may have just started it)."""
    start = time.time()
    while not os.path.exists(SOCKET):
        if time.time() - start > timeout:
            raise TimeoutError(f"no QMP socket at {SOCKET}; is refharness/vm.sh running?")
        time.sleep(0.2)
    return QMP()


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    vm = connect()
    verb, rest = argv[1], argv[2:]
    if verb == "screendump":
        print(vm.screendump(rest[0]))
    elif verb == "type":
        vm.type(rest[0])
    elif verb == "key":
        vm.keys(*rest[0].split("-"))
    elif verb == "run":
        vm.run(rest[0])
    elif verb == "powerdown":
        vm.command("system_powerdown")
    elif verb == "wait":
        print(f"desktop after {wait_for_desktop(vm):.0f}s")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
