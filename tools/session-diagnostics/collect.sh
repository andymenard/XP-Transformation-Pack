#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Read-only diagnostics. Run after a slow logout/login, before rebooting.
set -u
report=${1:-"$HOME/xplasma-session-diagnostics-$(date +%Y%m%d-%H%M%S).txt"}
if [ -e "$report" ]; then echo "Refusing to overwrite $report" >&2; exit 1; fi
umask 077
section() {
 printf '\n=== %s ===\n' "$1"
 shift
 timeout 15s "$@" 2>&1 || true
}
{
 date --iso-8601=seconds
 section Platform cat /etc/os-release
 section 'Plasma version' plasmashell --version
 section 'User session startup chain' systemd-analyze --user critical-chain
 section 'Failed user services' systemctl --user --failed --no-pager
 section 'Session helper services' systemctl --user status xp-session-theme.service xp-session-theme.path xp-cursor-sync.service plasma-kwin_wayland.service plasma-plasmashell.service --no-pager
 section 'Recent user-session journal (monotonic timestamps)' journalctl --user -b -n 1800 -o short-monotonic --no-pager
 section 'Display manager journal (may require journal access)' journalctl -b -u sddm.service -n 250 -o short-monotonic --no-pager
 section 'Session stop/start timeout messages' journalctl -b -g 'stop job|Timed out|timed out|Failed to stop|Session .*logged out|session .*logged out|Stopping User Manager|Starting User Manager' -n 200 -o short-monotonic --no-pager
} > "$report"
printf 'Saved %s\nReview the report before sharing; it includes account/application names and session logs.\n' "$report"
