# Credits and notice audit — 2026-10-17

Scope: the public Alpha 1.0 source, installer packaging/export paths, native
Properties resources, and the locally supplied alpha.5 installer’s notice and
TOMLKit files. This is a dependency and attribution inventory, not a new grant
of rights to third-party assets.

## Findings addressed

- The release exporter preferred POSTING-CREDITS.md over product credits.
  Both export paths now use the finished root CREDITS.md without appending
  posting instructions or duplicate scope paragraphs.
- Installer credits listed historical applications excluded from this alpha.
  The product credits now describe included components and optional fonts.
- Properties embedded native/THIRD-PARTY-NOTICES, while the installer shipped a
  different extension of that file. Both now contain the same notices, credits,
  project license and individual third-party license texts.
- TOMLKit was named in the installer notices but its MIT text was only available
  inside its Python distribution. That exact text is now accessible in both UIs.
- RetroThemesFirefox's BSD notice and the CC0 icon dedication existed elsewhere
  in the tree but were inaccessible from the legal dialogs; both are now bundled
  with those dialogs.
- Optional MS Sans Serif fonts were credited as CC-BY-SA-3.0 without an accessible
  full license in the central notice bundle. That license is now included.
- Session Logout.qml (Aleix Pol Gonzalez, 2014) and timer.js (David Edmundson,
  2018) retain GPL-2.0-or-later source headers. The central notices now identify
  them and include GPL version 2. Lock-screen MainBlock.qml retains David
  Edmundson's 2016 LGPL-2.0-or-later header; its license is included centrally.
- KDE logo and Breeze artwork retain their existing LGPL-2.1 and CC-BY-SA-4.0
  notices and per-asset provenance. These license texts are now accessible from
  both dialogs. Tux's acknowledgement and redistribution notice is retained.
- Pillow, fontTools, freetype-py, compression libraries and the boot/login runtime
  projects are acknowledged as system or development dependencies, not presented
  as newly vendored code. Their own distributions retain their license notices.

## Evidence and retained notices

- libarchive source and configuration: native/src/third_party/; libarchive and
  bzip2 notices remain intact. Literal templates in upstream license texts,
  including libarchive's `<author(s)>`, are preserved verbatim.
- TOMLKit: lib/tomlkit-0.14.0.dist-info/licenses/LICENSE in the supplied installer.
- Firefox theme: browser/ie6/LICENSE-RetroThemesFirefox.
- Fonts: tools/crisp-text/CREDITS.md and the supplied font metadata.
- Icons: tools/icon-expansion/CREDITS.md and LICENSES/.
- KDE adaptations: themes/xp-session/contents/logout/ and
  themes/xp-lockscreen/MainBlock.qml.
- Logos: native/src/app/editor/logos/COPYING-TUX and COPYING-KDE-LOGO.
- CC-BY-SA license texts: SPDX license-list-data/text/CC-BY-SA-{3.0,4.0}.txt;
  GNU and CC0 texts: distribution-provided /usr/share/common-licenses/.

Original Microsoft resources, captured Tahoma letterforms and supplied stock
photographs retain separate rights. Unknown photographer metadata remains
identified as unresolved; no authors or redistribution permissions were guessed.
Excluded applications are outside this alpha audit. Separately supplied Firefox
and system runtime distributions retain their own complete component notices.

## Maintaining consistency

Edit root CREDITS.md, native/THIRD-PARTY-NOTICES and native/LICENSES/, then run:

```bash
python3 tools/sync-legal.py
python3 tools/sync-legal.py --check
```

The release exporter and installer payload synchronizer reject stale legal
copies. Native standalone source archives carry the synchronized copies.
A new installer/native build is needed for existing installations to show the
updated dialogs; this source change does not rewrite already released archives.
