#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The Tigris CLI (pinned), signed in with the CI's secrets the first time:
# TIGRIS_STORAGE_ACCESS_KEY_ID and TIGRIS_STORAGE_SECRET_ACCESS_KEY, an
# Editor key for the bucket.
#
#   ci/tigris.sh cp FILE t3://BUCKET/KEY
set -eu
cli() { npx -y @tigrisdata/cli@3.13.0 "$@"; }
if [ ! -f "$HOME/.tigris/config.json" ]; then
    : "${TIGRIS_STORAGE_ACCESS_KEY_ID:?no Tigris access key: set it in the repository's secrets}"
    : "${TIGRIS_STORAGE_SECRET_ACCESS_KEY:?no Tigris secret: set it in the repository's secrets}"
    # (--endpoint too: without a terminal, configure asks for it otherwise --
    # "requires interactive input" -- default or not)
    cli configure --access-key "$TIGRIS_STORAGE_ACCESS_KEY_ID" --access-secret "$TIGRIS_STORAGE_SECRET_ACCESS_KEY" \
        --endpoint https://t3.storage.dev >/dev/null
fi
cli "$@"
