"""Pinned upstream sources -> static musl tools, notices and binary manifest."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parent
SOURCES = json.loads((ROOT / "sources.json").read_text())


def source_file(spec):
    return ROOT / "sources" / spec["url"].rsplit("/", 1)[1]


def verify(path, expected):
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != expected:
        raise RuntimeError(f"SHA-256 mismatch for {path.name}: {actual}")


def fetch():
    (ROOT / "sources").mkdir(exist_ok=True)
    for spec in SOURCES.values():
        path = source_file(spec)
        if not path.exists():
            temporary = path.with_suffix(path.suffix + ".part")
            try:
                with urllib.request.urlopen(spec["url"], timeout=60) as response, temporary.open("wb") as out:
                    shutil.copyfileobj(response, out)
                verify(temporary, spec["sha256"])
                temporary.replace(path)
            finally:
                temporary.unlink(missing_ok=True)
        verify(path, spec["sha256"])


def build(output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="xp2plasma-tools-") as temporary:
        work = Path(temporary)
        prefix = work / "prefix"
        trees = {}
        for name, spec in SOURCES.items():
            archive = source_file(spec)
            verify(archive, spec["sha256"])
            tree = work / name
            tree.mkdir()
            with tarfile.open(archive) as tf:
                tf.extractall(tree, filter="data")
            trees[name] = next(tree.iterdir())
        env = {**os.environ, "CFLAGS": "-Os -fno-ident -std=gnu17", "LDFLAGS": "-static",
               "CPPFLAGS": f"-I{prefix}/include", "PKG_CONFIG_LIBDIR": f"{prefix}/lib/pkgconfig",
               "SOURCE_DATE_EPOCH": "1788912000"}
        jobs = str(min(os.cpu_count() or 2, 8))

        def run(args, cwd):
            print("+", " ".join(map(str, args)), flush=True)
            subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=True)

        run(["./configure", f"--prefix={prefix}", "--static"], trees["zlib"])
        run(["make", "-j" + jobs], trees["zlib"])
        run(["make", "install"], trees["zlib"])
        run(["make", "-j" + jobs, "libbz2.a", "CFLAGS=-Os"], trees["bzip2"])
        shutil.copy2(trees["bzip2"] / "libbz2.a", prefix / "lib")
        shutil.copy2(trees["bzip2"] / "bzlib.h", prefix / "include")
        run(["./configure", f"--prefix={prefix}", "--disable-shared", "--enable-static",
             "--disable-nls", "--disable-xz", "--disable-xzdec", "--disable-lzmadec",
             "--disable-lzmainfo", "--disable-scripts", "--disable-doc"], trees["xz"])
        run(["make", "-j" + jobs], trees["xz"])
        run(["make", "install"], trees["xz"])
        run(["make", "-C", "lib", "-j" + jobs, "libzstd.a"], trees["zstd"])
        shutil.copy2(trees["zstd"] / "lib/libzstd.a", prefix / "lib")
        for header in ("zstd.h", "zstd_errors.h"):
            shutil.copy2(trees["zstd"] / "lib" / header, prefix / "include")

        run(["cmake", "-S", ".", "-B", "build", "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=MinSizeRel", f"-DCMAKE_PREFIX_PATH={prefix}",
             "-DCMAKE_EXE_LINKER_FLAGS=-static", "-DBUILD_SHARED_LIBS=OFF",
             "-DENABLE_TEST=OFF", "-DENABLE_TAR=ON", "-DENABLE_CPIO=OFF",
             "-DENABLE_CAT=OFF", "-DENABLE_UNZIP=OFF", "-DENABLE_OPENSSL=OFF",
             "-DENABLE_MBEDTLS=OFF", "-DENABLE_NETTLE=OFF", "-DENABLE_LIBXML2=OFF",
             "-DENABLE_EXPAT=OFF", "-DENABLE_ICONV=OFF", "-DENABLE_ACL=OFF",
             "-DENABLE_XATTR=OFF", "-DENABLE_LZ4=OFF", "-DENABLE_LZO=OFF"], trees["libarchive"])
        run(["cmake", "--build", "build", "--target", "bsdtar", "-j", jobs], trees["libarchive"])
        run(["./configure", "--disable-nls", "--without-external-libmspack"], trees["cabextract"])
        run(["make", "-j" + jobs], trees["cabextract"])
        binaries = {"bsdtar": trees["libarchive"] / "build/bin/bsdtar",
                    "cabextract": trees["cabextract"] / "cabextract"}
        manifest = {"architecture": platform.machine(), "linkage": "static-musl", "tools": {},
                    "sources": SOURCES,
                    "build_packages": subprocess.check_output(["apk", "--no-network", "info", "-v"], text=True).splitlines()}
        for name, binary in binaries.items():
            run(["strip", binary], work)
            headers = subprocess.check_output(["readelf", "-l", binary], text=True)
            dynamic = subprocess.check_output(["readelf", "-d", binary], text=True)
            if "INTERP" in headers or "NEEDED" in dynamic:
                raise RuntimeError(f"{name} is not fully static")
            target = output / name
            shutil.copy2(binary, target)
            target.chmod(0o755)
            version = subprocess.check_output([str(target), "--version"], text=True).strip()
            manifest["tools"][name] = {"sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
                                       "version": version}
        # Preserve upstream notices and full source archives alongside the tools.
        notices = output / "licenses"
        notices.mkdir(exist_ok=True)
        for name, tree in trees.items():
            dest = notices / name
            dest.mkdir(exist_ok=True)
            for file in tree.rglob("*"):
                if file.is_file() and file.name.upper().startswith(("COPYING", "LICENSE", "COPYRIGHT")):
                    target = dest / file.relative_to(tree)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(file, target)
        # Alpine supplies the toolchain libc; retain upstream's notice too.
        shutil.copy2(trees["musl"] / "COPYRIGHT", notices / "musl-COPYRIGHT")
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--build", metavar="OUTPUT")
    args = parser.parse_args()
    if args.fetch:
        fetch()
    if args.build:
        build(args.build)
