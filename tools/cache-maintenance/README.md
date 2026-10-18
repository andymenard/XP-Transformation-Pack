# Retention for disposable XPlasma data

Default policy:

| Data | Count target | Size target |
| --- | ---: | ---: |
| Unused generated look packages (GTK/Plasma/colors/start/decoration together) | 16 | 256 MiB |
| Unused dark application builds | 8 | 128 MiB |
| Completed maintenance snapshots | 5 | 256 MiB |

Both count and byte limits apply, evicting oldest unused entries first. Active
and referenced resources are protected. Saved .theme and .msstyles files,
original assets/downloads, compiler inputs, and installer uninstall/rollback
stores are outside the cleanup scope. The two rotating Plasma slots are retained.

Maintenance backups are eligible only with a completed maintenance marker, or a
recognized timestamped Snapshot (valid before/after/tracked JSON). Incomplete,
unknown, pinned (`.keep`) and installer-referenced backups are protected. The
newest completed backup is always retained, even if it exceeds the byte target;
completed transactions have a ten-minute grace period for safe rollback.
Protected data can therefore exceed the targets. Unmarked baseline backups are
not disposable. `--adopt-legacy` explicitly includes older timestamped ad-hoc
development backups for a one-time cleanup; incomplete/pinned data still stays.

Native theme application and dark-mode changes run this policy. Preview cleanup
is throttled to once a minute. The installer Snapshot class marks maintenance
transactions incomplete/complete and runs retention after completion; normal
installer recovery folders are never enrolled. No background daemon is needed.
Symlinked roots are skipped, nested links are not followed, and a nonblocking
file lock prevents two policy executions from deleting the same resources.

Dry run (default):

```
python3 cleanup.py --home "$HOME"
```

Apply the plan with `--apply`. Use `.keep` inside a maintenance backup to pin it.
`sync.py` copies this authoritative policy into the native embedded resource and
installer module. Run the retention tests with:

```
python3 -m unittest discover -s installer/tests -p test_retention.py -v
```

Theme artwork caching is opt-in (`settings.themeCacheEnabled` in XPlasma's
state). Properties asks on the first theme Apply unless its Appearance checkbox
was chosen explicitly. Yes/No is remembered; Cancel applies nothing. The same
checkbox can enable or disable caching later. Until enabled, the unused-look
and unused-dark-build targets are zero. Active/referenced artwork and the last
look protected during a switch remain available; saved/imported theme sources,
original assets and recovery backups are preserved. When enabled, the bounded
16-look/8-dark-build policy above applies. Recovery-backup limits are independent
of this preference. This setting does not disable Qt/KDE's internal caches or
the installer's compiler/prebuilt caches.
