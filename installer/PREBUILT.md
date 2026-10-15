# Faster installation

The installer uses a verified bundled runtime when its distribution release,
architecture, Qt minor, Plasma series and native source contents match. The
caption effect also requires the exact KWin version and matching effect source.
Every runtime file is checked against the package manifest. Missing libraries,
changed files, incompatible machines or new source select a local build instead.
No unsigned executable downloads are performed during installation.

For troubleshooting, launch Setup with `XP_BUILD_FROM_SOURCE=1` to bypass
prebuilt packages and use the installed compiler and SDK instead.

Completed native builds are saved under `~/.cache/xplasma-plus/prebuilt` for reuse
after removal/reinstallation. Compiler parallelism follows available CPUs and
memory (up to eight jobs), with `XP_BUILD_JOBS` as an explicit override. The Start
helper is built once as part of the native project. Compatible prebuilt installs
skip native and widget development dependencies; a matching caption binary also
skips the KWin development dependencies.

Run the **Build Kubuntu prebuilt components** GitHub workflow to build the
Intel/AMD package using Ubuntu 26.04 libraries. It builds the core, four Plasma
plugins and caption effect; the Plasma source archives have pinned checksums.
It produces a tar archive inside the workflow artifact, preserving executable
permissions and links. Extract its hash directories into `private-assets/prebuilt/`, then run `installer/sync-payload.py`
and the normal signed release packager. These steps bundle the binaries in the
installer, so installation does not need a GitHub account or a build download.
Kubuntu testing is still required before publishing a new prebuilt variant.

On any supported native build host, a matching output can also be exported:

```sh
python3 installer/build_cache.py store /path/to/complete-installer /path/to/build taskbar --destination private-assets/prebuilt
```

Add `--caption /path/to/xp-caption-animation.so` only for a binary built from the
bundled effect source against that host's KWin. Linux plugin sources and license
notices remain included in the complete installer.

## Component reuse

The four Plasma widget plugins have independent cache entries. Their identities
cover the distribution, architecture, Qt/Plasma series, patched widget files,
shared project files and build recipe. A notification-only QML edit invalidates
the notification plugin; shared C++ or CMake changes invalidate dependent plugins.
Each cached binary is checked for its hash, ELF architecture and missing symbols
before reuse. The animation effect has a separate entry tied to its own sources
and the exact KWin package, independent of engine and widget edits.

Engine tools share embedded resources, so some widget QML edits also require
relinking those tools. Existing CMake build directories are retained, allowing
incremental compilation instead of starting over. Native source extraction uses
a content hash, so archive timestamp changes alone do not discard work.

Kubuntu CI restores previous build caches before compiling and exports individual
widget/effect entries alongside the complete runtime. The same installer cache
logic is used on Fedora and other supported KDE distributions. Unmatched or
modified entries fall back to a build. Set `XP_BUILD_CACHE` to choose a cache
folder; `XP_BUILD_FROM_SOURCE=1` bypasses all binary cache lookups.

Updating scripts, Firefox CSS or supplied assets still requires regenerating the
installer inventory/archive and signing release metadata, but no native build
when native inputs are unchanged. Existing schema-1 complete packages remain
supported; component caches are populated by the updated builder.
