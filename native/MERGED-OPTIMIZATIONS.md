# Workspace optimization merge — 2026-10-16

Imported the newer native source snapshot from the shared XPlasma workspace into
the canonical transformation-pack source tree. Existing upstream copyright and
license notices are retained. No developer build directory or host-native binary
was imported. The precompiled Qt shader resources remain alongside their shader
sources for compatibility components.

Included changes:

- Static custom-color generation, embedded editable preferences/provenance and
  saved-profile restoration without double tinting.
- Cache lookup before artwork decoding, worker-based/coalesced editing and deferred
  hidden previews.
- Color-only application, immutable versioned assets, bounded cleanup and merged
  SVG hints instead of discarding the global SVG cache.
- Native decoration support when its matching development libraries are available;
  the Aurorae fallback remains available.
- Optimized Start helper builds and a versioned QML helper module.
- Compositor-driven caption animation, cached caption labels and configuration
  watching, with the existing KWin 6.6 adapters, ABI checks and credits retained.

Integration adjustments:

- `xplasma-runtime` builds all installed runtime targets, including the optional
  native decoration, before installer staging.
- Alpha native setup discovers the helper module from its qmldir instead of
  hardcoding its old directory.
- Public source export removes retired tracked native resources so public build
  inputs match the canonical source tree.
- Tests use the packaged QML helper, isolate preview caches, cover retired tint
  settings, and handle both native and Aurorae decoration backends.

Validation outputs are under `/tmp/xp-merged-*.log` on the merge host. Native
builds and staging remain inside the workspace; no desktop installation, theme
application, release publication or OTA update is performed by this merge.
