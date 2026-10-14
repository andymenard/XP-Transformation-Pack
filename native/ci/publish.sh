#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# build/XPlasma-Setup into the Tigris bucket (public: every object has an
# address under TIGRIS_PUBLIC_URL, e.g. https://BUCKET.t3.tigrisfiles.io/KEY).
#
#   every build      builds/COMMIT/XPlasma-Setup, and builds/trunk/XPlasma-Setup
#                    (the newest)
#   and when project.ini's Version isn't released yet, the build is that
#   release: releases/N/XPlasma-Setup and its SHA256SUMS,
#   latest/XPlasma-Setup, and latest.json pointed at it -- what the update
#   check reads (project.ini's UpdatesUrl), its notes project.ini's Notes
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
echo "build ${TANGLED_SHA:-local}:"
put "$setup" "builds/${TANGLED_SHA:-local}/XPlasma-Setup" 'public, max-age=31536000, immutable'
put "$setup" builds/trunk/XPlasma-Setup 'public, max-age=300'

# A Version not yet released: this build is that release.
if curl -fsI -o /dev/null "$base/releases/$version/XPlasma-Setup"; then
    echo "release $version is out already (a new one: Version one higher)"
    exit 0
fi
echo "release $version:"
put "$setup" "releases/$version/XPlasma-Setup" 'public, max-age=31536000, immutable'
printf '%s  XPlasma-Setup\n' "$sha" > build/SHA256SUMS
put build/SHA256SUMS "releases/$version/SHA256SUMS" 'public, max-age=31536000, immutable'
put "$setup" latest/XPlasma-Setup 'public, max-age=300'
notes="$(sed -n 's/^Notes=//p' project.ini)"
jq -n --argjson version "$version" --arg page "$base/releases/$version/XPlasma-Setup" --arg notes "$notes" --arg sha256 "$sha" \
    '{version: $version, page: $page, notes: $notes, sha256: $sha256}' > build/latest.json
put build/latest.json latest.json 'public, max-age=300'
