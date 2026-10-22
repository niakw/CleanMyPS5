#!/usr/bin/env bash
# Git object identity pins the exact decoder source. No unverified releases.
set -euo pipefail
dest=${1:?usage: fetch_stb_decoder.sh DESTINATION_DIRECTORY}
revision=a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d
temp=$(mktemp -d)
trap 'rm -rf -- "$temp"' EXIT
git clone --quiet --filter=blob:none https://github.com/nothings/stb.git "$temp/stb"
git -C "$temp/stb" checkout --quiet --detach "$revision"
test "$(git -C "$temp/stb" rev-parse HEAD)" = "$revision" || {
  echo 'Image decoder revision mismatch' >&2;exit 1;
}
test -s "$temp/stb/stb_image.h"
mkdir -p "$dest"
cp "$temp/stb/stb_image.h" "$dest/stb_image.h"
echo "Pinned image decoder source verified: $revision"
