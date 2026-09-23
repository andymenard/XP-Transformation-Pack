# Private archive utilities

`cabextract` and `bsdtar` are built from hash-pinned upstream sources as static
musl Linux executables. Releases currently provide x86_64 binaries. Build on
another Linux architecture to produce its matching directory; do not relabel
an x86_64 binary as an ARM build.

From the repository root:

```sh
sh bundled-tools/build.sh
python3 bundled_tools.py
```

The build uses Podman (or `TOOLS_ENGINE=docker`), a digest-pinned Alpine base,
and its build tools. Source downloads are verified against `sources.json`.
Compilation runs with networking disabled, baseline CPU settings, and static
linking. The builder rejects ELF interpreters/shared-library dependencies and
records binary hashes, versions, architecture, and build package versions in
`<architecture>/manifest.json`. Package repositories can change, so this is a
versioned build recipe, not a promise of bit-identical toolchains forever.

bsdtar includes zlib, bzip2, liblzma and zstd, with built-in ISO/ZIP/tar/RAR
readers. External crypto, XML, ACL/xattr, LZ4 and LZO libraries are disabled;
this is an extraction utility for setup, not a full system backup tool.
cabextract uses its bundled libmspack. Both tools run on glibc hosts without
requiring a musl installation.

`sources/` contains the original source archives, including cabextract's
corresponding source, and is distributed alongside the binaries and this
build recipe. `<architecture>/licenses/` retains upstream license notices.
musl is supplied by the Alpine compiler environment; its upstream source and
notice are included, with Alpine package versions recorded in the manifest.
Keep these files with redistributed binaries. No Windows assets are included.

Runtime lookup is restricted to these two names. The verified binary is run in
place, from wherever the app tree happens to live — a source checkout, the
installed copy under `~/.local`, or the AppImage mount. No PATH or library-path
changes are made. A damaged shipped binary causes a clear error rather than
falling back silently to a different utility. Development checkouts without
built utilities retain the host-tool fallback.

Packaging refuses to ship a missing or damaged pair: the checksums are verified
on every lookup, and `appimage/build.sh` runs both tools before it will build.
Upgrades to these
dependencies require updating the source lock, rebuilding, and rerunning
`tests/test_bundled_tools.py` and `tests/test_setup.py`. Bundling means we own
security updates for these utilities instead of relying on host packages.
