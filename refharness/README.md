# refharness/

The pixel oracle: real Windows XP draws visual-style parts, and XPLasma's
renderer has to match what comes back. PLAN.md §7.

| File | Runs on | What |
| --- | --- | --- |
| `vm.sh` | host | Boots `~/clod/xpvm/xp.sh` with a QMP socket and `-snapshot` (test runs never write to `xp.qcow2`). `XPVM_PERSIST=1` to change the VM for real. |
| `vm.py` | host | QMP client: `screendump`, `type`, `key`, `run`, `wait`, `powerdown`. **One connection per process**: QMP serves one client at a time, and a second connect blocks. |
| `oracle.py` | host | Runs a job: builds an ISO, hot-swaps it into XP's CD drive, XP autoruns it, results come back over HTTP, and the black/white BMP pairs become RGBA PNGs. |
| `uxdump.c` | XP | Draws `class part state w h` with real uxtheme, over black and over white, and records uxtheme's metrics. Loads non-applied styles through uxtheme's ordinals 2/3/16 (`OpenThemeFile`, `CloseThemeFile`, `OpenThemeDataFromFile`). |
| `xput.c` | XP | PUTs a directory's files to the host (`10.0.2.2:8765`). |
| `build.sh` | host | Cross-compiles both in the dev container: 32-bit, **no C runtime** (mingw-w64 now links the UCRT, which XP lacks), XP 5.1 subsystem. |

```sh
refharness/build.sh
refharness/vm.sh &                     # boot (≈50 s to a settled desktop)
.venv/bin/python3 refharness/vm.py wait
.venv/bin/python3 refharness/oracle.py job.txt out/ --style path/to/Royale.msstyles
```

A job:

```
style -                                                   # the applied style
part Button 1 1 75 23
style {style:Royale.msstyles} NormalColor NormalSize      # a style by file
part Button 1 2 75 23
```

Outputs are prefixed `s<N>_` by style section (`s1` = the first `style` line).

## What we've learned from it

- Transport: QEMU's read-write `vvfat` crashes on the guest's first write
  ("cluster 0 used more than once"), so jobs go in on an ISO and results come
  out over HTTP. A round trip takes about 2 s.
- Semi-transparent pixels: recovering RGBA from the two renders is only exact
  to ±1 per channel (XP blends premultiplied). **Compare composited renders**,
  ours over black and white against uxdump's BMPs, not un-premultiplied RGBA.
- uxtheme stretches without filtering. On Royale's push button every opaque
  pixel of a naive nine-slice already matches; the ±1 differences sit on the
  semi-transparent outer ring and come from filtering plus rounding.
