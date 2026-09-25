#!/usr/bin/env python3
"""Ask real Windows XP to draw visual-style parts: XPlasma's pixel oracle.

    oracle.py JOB OUTDIR [--style FILE.msstyles ...]

JOB is a uxdump job (see uxdump.c). Style files named with --style are
carried into the VM, and a job refers to one as `{style:NAME}`, e.g.

    style {style:Royale.msstyles} NormalColor NormalSize
    part Button 1 1 75 23

What happens, with the VM already running (refharness/vm.sh):

1. The job, uxdump.exe, xput.exe, the styles and a GO.CMD are written to an
   ISO, and QMP swaps it into XP's CD drive.
2. XP autoruns GO.CMD (or, if autorun doesn't fire, it is started from
   the Run dialog). GO.CMD copies everything to C:\\xpl\\<run>, runs uxdump
   there, then xput.exe sends the results to this script's HTTP receiver
   on 10.0.2.2:8765.
3. Each black/white BMP pair becomes one exact RGBA PNG: alpha is 255 minus
   the white-over-black difference, colour is the black render un-premultiplied.

OUTDIR gets the PNGs, the raw BMPs (under raw/) and uxdump's metrics.txt.
"""
from __future__ import annotations

import argparse
import http.server
import io
import os
import sys
import tempfile
import threading
import time
import uuid

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from vm import QMP, connect  # noqa: E402

PORT = 8765


class Receiver(http.server.ThreadingHTTPServer):
    """Accepts `PUT /<name>` into a directory; `PUT /.started` says the job is
    running, `PUT /.done` ends the batch."""

    def __init__(self, directory: str) -> None:
        self.directory = directory
        self.started = threading.Event()
        self.done = threading.Event()
        self.summary = ""
        super().__init__(("127.0.0.1", PORT), _Handler)


class _Handler(http.server.BaseHTTPRequestHandler):
    def do_PUT(self) -> None:  # noqa: N802 -- http.server's spelling
        name = os.path.basename(self.path.lstrip("/"))
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if name == ".started":
            self.server.started.set()
        elif name == ".done":
            self.server.summary = body.decode(errors="replace")
            self.server.done.set()
        elif name:
            with open(os.path.join(self.server.directory, name), "wb") as fh:
                fh.write(body)
        self.send_response(201)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, *_args) -> None:
        pass


def _iso(files: dict[str, bytes]) -> bytes:
    """An ISO9660+Joliet image holding `files` (name -> bytes) at its root."""
    import pycdlib
    iso = pycdlib.PyCdlib()
    iso.new(joliet=3, vol_ident="XPLASMA")
    for i, (name, data) in enumerate(sorted(files.items())):
        stem, _, ext = name.upper().rpartition(".")
        short = f"{(''.join(c for c in stem if c.isalnum()) or 'F')[:6]}{i:02d}.{ext[:3] or 'BIN'};1"
        iso.add_fp(io.BytesIO(data), len(data), f"/{short}", joliet_path=f"/{name}")
    out = io.BytesIO()
    iso.write_fp(out)
    iso.close()
    return out.getvalue()


def _cd_device(vm: QMP) -> str:
    for dev in vm.command("query-block"):
        if dev.get("removable") and "cd" in (dev.get("qdev", "") + dev.get("device", "")).lower():
            return dev.get("qdev") or dev["device"]
    raise RuntimeError("the VM has no CD drive")


def pair_to_rgba(black: Image.Image, white: Image.Image) -> Image.Image:
    """Recover the exact RGBA uxtheme drew from renders over black and white."""
    b = black.convert("RGB").tobytes()
    w = white.convert("RGB").tobytes()
    out = bytearray(len(b) // 3 * 4)
    for i in range(0, len(b), 3):
        # Over black a channel reads c*a; over white c*a + 255*(1-a). The
        # difference is 255*(1-a) in every channel -- average the three.
        diff = (w[i] - b[i] + w[i + 1] - b[i + 1] + w[i + 2] - b[i + 2]) / 3
        alpha = max(0, min(255, round(255 - diff)))
        j = i // 3 * 4
        if alpha:
            out[j:j + 3] = bytes(min(255, round(c * 255 / alpha)) for c in b[i:i + 3])
        out[j + 3] = alpha
    return Image.frombytes("RGBA", black.size, bytes(out))


def run(job_text: str, outdir: str, styles: list[str], timeout: float = 120.0, progress=print,
        vm: QMP | None = None) -> str:
    """Run one job in the VM; returns OUTDIR. Pass `vm` to reuse a connection:
    QMP serves one client at a time, so a second one would wait forever."""
    run_id = uuid.uuid4().hex[:8]
    guest_dir = f"C:\\xpl\\{run_id}"
    files: dict[str, bytes] = {}
    for style in styles:
        name = os.path.basename(style)
        with open(style, "rb") as fh:
            files[name] = fh.read()
        # {style:NAME} and {file:NAME} both name a carried file inside the VM.
        for kind in ("style", "file"):
            job_text = job_text.replace(f"{{{kind}:{name}}}", f"{guest_dir}\\in\\{name}")
    for tool in ("uxdump.exe", "xput.exe"):
        with open(os.path.join(HERE, "build", tool), "rb") as fh:
            files[tool] = fh.read()
    files["job.txt"] = job_text.replace("\r\n", "\n").replace("\n", "\r\n").encode("ascii")
    files["go.cmd"] = (
        "@echo off\r\n"
        f"mkdir {guest_dir}\\in\r\n"
        f"xcopy /q /y %~d0\\*.* {guest_dir}\\in\\ >nul\r\n"
        f"{guest_dir}\\in\\xput.exe -started 10.0.2.2 {PORT}\r\n"
        f"{guest_dir}\\in\\uxdump.exe {guest_dir}\\in\\job.txt {guest_dir}\\out\r\n"
        f"{guest_dir}\\in\\xput.exe {guest_dir}\\out 10.0.2.2 {PORT}\r\n"
    ).encode("ascii")
    files["autorun.inf"] = b"[autorun]\r\nopen=cmd.exe /c go.cmd\r\n"

    os.makedirs(os.path.join(outdir, "raw"), exist_ok=True)
    receiver = Receiver(os.path.join(outdir, "raw"))
    thread = threading.Thread(target=receiver.serve_forever, daemon=True)
    thread.start()
    own = vm is None
    vm = vm or connect()
    iso_path = None
    try:
        with tempfile.NamedTemporaryFile(suffix=".iso", delete=False) as fh:
            fh.write(_iso(files))
            iso_path = fh.name
        device = _cd_device(vm)
        progress(f"run {run_id}: swapping the job disc into {device}")
        vm.command("blockdev-change-medium", id=device, filename=iso_path, format="raw")
        started = time.time()
        # Autorun announces itself; only if it never does, start the job by
        # hand -- a second copy of a long job would run over the first.
        if not receiver.started.wait(20) and not receiver.done.is_set():
            progress("  autorun didn't start in 20s; starting it from the Run dialog")
            vm.run("cmd.exe /c D:\\go.cmd")
        if not receiver.done.wait(max(1.0, timeout - (time.time() - started))):
            vm.screendump(os.path.join(outdir, "timeout.png"))
            raise TimeoutError(f"no results after {timeout:.0f}s; see {outdir}/timeout.png")
        progress(f"  {receiver.summary.split()[0]} files back in {time.time() - started:.1f}s")
    finally:
        receiver.shutdown()
        receiver.server_close()
        try:
            vm.command("eject", id=_cd_device(vm), force=True)
        finally:
            if own:
                vm.close()
            if iso_path:
                os.unlink(iso_path)

    raw = os.path.join(outdir, "raw")
    for name in sorted(os.listdir(raw)):
        if name.endswith("_black.bmp"):
            stem = name[: -len("_black.bmp")]
            white = os.path.join(raw, f"{stem}_white.bmp")
            if os.path.exists(white):
                pair_to_rgba(Image.open(os.path.join(raw, name)), Image.open(white)).save(
                    os.path.join(outdir, f"{stem}.png"))
    if os.path.exists(os.path.join(raw, "metrics.txt")):
        os.replace(os.path.join(raw, "metrics.txt"), os.path.join(outdir, "metrics.txt"))
    return outdir


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("job")
    ap.add_argument("outdir")
    ap.add_argument("--style", "--file", action="append", default=[], dest="style",
                    help="a file (.msstyles, .bmp, ...) to carry into the VM; refer to it as {style:NAME} or {file:NAME}")
    ap.add_argument("--timeout", type=float, default=120.0)
    args = ap.parse_args(argv[1:])
    with open(args.job, encoding="utf-8") as fh:
        job = fh.read()
    run(job, args.outdir, args.style, args.timeout)
    print(open(os.path.join(args.outdir, "metrics.txt"), encoding="latin-1").read())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
