#!/usr/bin/env bash
# Make a drag-to-Applications image from a deployed bundle.
set -euo pipefail

if [[ $# != 2 || ! -x "$1/Kadro.app/Contents/MacOS/Kadro" || "$2" != *.dmg ]]; then
  echo "Usage: $0 DIST OUTPUT.dmg (DIST must contain the deployed Kadro.app)" >&2
  exit 2
fi

dist="$1"
output="$2"
workspace="$(mktemp -d)"
trap 'rm -rf "$workspace"' EXIT
stage="$workspace/volume"
mkdir "$stage"

ditto "$dist/Kadro.app" "$stage/Kadro.app"
ln -s /Applications "$stage/Applications"
cp "$dist/LICENSE" "$dist/THIRD_PARTY.md" "$dist/docs/INSTALL.md" "$stage/"
hdiutil create -srcfolder "$stage" -volname "Wanted View" -format UDZO "$workspace/WantedView.dmg"
mv "$workspace/WantedView.dmg" "$output"
