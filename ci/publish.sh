#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# build/XPlasma-Setup into the Tigris bucket (public: every object has an
# address under TIGRIS_PUBLIC_URL, e.g. https://BUCKET.t3.tigrisfiles.io/KEY).
#
#   a push to trunk  builds/COMMIT/XPlasma-Setup, and builds/trunk/XPlasma-Setup
#                    (the newest)
#   a tag vN         release N -- N must be project.ini's Version, not yet
#                    released: releases/N/XPlasma-Setup and its SHA256SUMS,
#                    latest/XPlasma-Setup, and latest.json pointed at it --
#                    what the update check reads (project.ini's UpdatesUrl);
#                    its notes are the tag's message (git tag -a vN -m ...)
set -eu
here="$(cd "$(dirname "$0")/.." && pwd)"
cd "$here"
: "${TIGRIS_BUCKET:?which Tigris bucket}"
setup=build/XPlasma-Setup
[ -x "$setup" ] || { echo "publish: no $setup (./package.sh)" >&2; exit 1; }
base="${TIGRIS_PUBLIC_URL:?the bucket'"'"'s public address}"
version="$(sed -n 's/^Version=//p' project.ini)"
sha="$(sha256sum "$setup" | cut -d' ' -f1)"
put() {   # FILE KEY CACHE-CONTROL
    ci/tigris.sh cp "$1" "t3://$TIGRIS_BUCKET/$2" --cache-control "$3"
    echo "  $base/$2"
}
case "${TANGLED_REF_TYPE:-}:${TANGLED_REF_NAME:-}" in
    tag:v*)
        [ "${TANGLED_REF_NAME#v}" = "$version" ] \
            || { echo "publish: the tag is $TANGLED_REF_NAME, but project.ini's Version is $version" >&2; exit 1; }
        ! curl -fsI -o /dev/null "$base/releases/$version/XPlasma-Setup" \
            || { echo "publish: release $version is out already: a new one needs Version one higher" >&2; exit 1; }
        echo "release $version:"
        put "$setup" "releases/$version/XPlasma-Setup" 'public, max-age=31536000, immutable'
        printf '%s  XPlasma-Setup\n' "$sha" > build/SHA256SUMS
        put build/SHA256SUMS "releases/$version/SHA256SUMS" 'public, max-age=31536000, immutable'
        put "$setup" latest/XPlasma-Setup 'public, max-age=300'
        # (the tag's message, if the clone has the tag; none is no failure)
        notes="$(git for-each-ref "refs/tags/$TANGLED_REF_NAME" --format='%(contents)' 2>/dev/null | sed '/^-----BEGIN PGP/,$d')"
        jq -n --argjson version "$version" --arg page "$base/releases/$version/XPlasma-Setup" --arg notes "$notes" --arg sha256 "$sha" \
            '{version: $version, page: $page, notes: $notes, sha256: $sha256}' > build/latest.json
        put build/latest.json latest.json 'public, max-age=300'
        ;;
    *)
        echo "build ${TANGLED_SHA:-local}:"
        put "$setup" "builds/${TANGLED_SHA:-local}/XPlasma-Setup" 'public, max-age=31536000, immutable'
        put "$setup" builds/trunk/XPlasma-Setup 'public, max-age=300'
        ;;
esac
