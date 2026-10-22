#!/usr/bin/env bash
# Native title build: never wraps the browser payload as an app.
set -euo pipefail

project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
title=${CMP5_NATIVE_TITLE_ID:-PPSA99784}
[[ $title =~ ^PPSA[0-9]{5}$ ]] || { echo 'Invalid title ID' >&2; exit 2; }
pin=470695e0c557f99ff2df0e36e4df713c5e636526
template_url=https://github.com/drakmor/ps5-native-app-boilerplate.git
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

for program in git make python3 zip; do
  command -v "$program" >/dev/null || { echo "Missing $program" >&2; exit 2; }
done

git clone --quiet "$template_url" "$tmp/native-toolchain"
git -C "$tmp/native-toolchain" checkout --quiet --detach "$pin"
test "$(git -C "$tmp/native-toolchain" rev-parse HEAD)" = "$pin" || {
  echo 'Pinned native build revision mismatch' >&2; exit 1;
}

# Overlay only CleanMyPS5-authored UI. The external toolchain stays in a
# temporary directory; its source/history/branding never enter this repo.
cp "$project/native/main.cpp" "$tmp/native-toolchain/src/main.cpp"
# The pinned starter draws one frame and sleeps forever. Enable actual
# double-buffered redraws before compiling the installed controller UI.
python3 "$project/scripts/enable_interactive_videoout.py" "$tmp/native-toolchain/src/demo_renderer.cpp"
python3 "$project/scripts/enable_native_preview_blit.py"   "$tmp/native-toolchain/src/demo_renderer.hpp" "$tmp/native-toolchain/src/demo_renderer.cpp"
cp "$project/native/loopback_protocol.hpp" "$tmp/native-toolchain/src/loopback_protocol.hpp"
# The read-only metadata scanner is compiled INTO the native title.
# It inherits title permissions; it never escalates them or installs a daemon.
cp "$project/src/native/inventory.c" "$tmp/native-toolchain/src/cmp5_inventory.c"
cp "$project/src/native/inventory.h" "$tmp/native-toolchain/src/inventory.h"
cp "$project/src/native/duplicates.c" "$tmp/native-toolchain/src/cmp5_duplicates.c"
cp "$project/src/native/duplicates.h" "$tmp/native-toolchain/src/duplicates.h"
cp "$project/src/native/homebrew.c" "$tmp/native-toolchain/src/cmp5_homebrew.c"
cp "$project/src/native/homebrew.h" "$tmp/native-toolchain/src/homebrew.h"
cp "$project/src/native/media_preview.c" "$tmp/native-toolchain/src/cmp5_media_preview.c"
cp "$project/src/native/media_preview.h" "$tmp/native-toolchain/src/media_preview.h"
cp "$project/native/preview_decode.c" "$tmp/native-toolchain/src/cmp5_preview_decode.c"
cp "$project/native/preview_decode.h" "$tmp/native-toolchain/src/preview_decode.h"
bash "$project/scripts/fetch_stb_decoder.sh" "$tmp/native-toolchain/src"
python3 "$project/scripts/embed_rules.py"   "$project/rules/metadata-v1.json" "$tmp/native-toolchain/src/scan_rules.h"
python3 "$project/scripts/make_native_icon.py" "$tmp/native-toolchain/sce_sys/icon0.png"
# Never distribute the toolchain demo title backgrounds/audio.
rm -f -- "$tmp/native-toolchain/sce_sys/pic0.dds" \
            "$tmp/native-toolchain/sce_sys/pic1.dds" \
            "$tmp/native-toolchain/sce_sys/snd0.at9"
rm -f -- "$tmp/native-toolchain/assets/banner.txt"
printf '%s\n' "CleanMyPS5 native UI - live double-buffered rendering, internal read-only scanner." \
  > "$tmp/native-toolchain/assets/native-readme.txt"
cp "$project/THIRD_PARTY_NOTICES.md" "$tmp/native-toolchain/assets/THIRD_PARTY_NOTICES.md"

make -C "$tmp/native-toolchain" init \
  TITLE_ID="$title" APP_NAME=CleanMyPS5 APP_CATEGORY=game
make -C "$tmp/native-toolchain" app

# Optional native-title package for compatible homebrew installers.
# This is NOT a retail-signed PKG; hardware support remains unverified.
if [[ ${CMP5_BUILD_FFPKG:-0} == 1 ]]; then
  command -v dotnet >/dev/null || {
    echo 'Optional homebrew FFPKG requires .NET SDK 8+' >&2; exit 2;
  }
  make -C "$tmp/native-toolchain" ffpkg
  test -s "$tmp/native-toolchain/dist/$title.ffpkg"
fi

# Keep privileged filesystem inspection in the separately launched payload,
# never in the installed title's sandbox, and never open WebKit.
PS5_PAYLOAD_SDK="$tmp/native-toolchain/.deps/native/ps5-payload-sdk" \
  make -C "$project" ps5-helper

python3 "$project/scripts/check_native_bundle.py" \
  "$tmp/native-toolchain/dist/$title" "$title"

mkdir -p "$project/dist/native"
cp "$project/cleanmyps5-helper.elf" "$project/dist/native/cleanmyps5-helper.elf"
if [[ ${CMP5_BUILD_FFPKG:-0} == 1 ]]; then
  cp "$tmp/native-toolchain/dist/$title.ffpkg" "$project/dist/native/$title.ffpkg"
fi
python3 - "$tmp/native-toolchain/dist/$title" "$project/dist/native/$title.zip" <<'PY'
from pathlib import Path
import sys
from zipfile import ZipFile, ZIP_DEFLATED
source = Path(sys.argv[1])
dest = Path(sys.argv[2])
with ZipFile(dest, "w", ZIP_DEFLATED, compresslevel=6) as archive:
    for file in sorted(source.rglob("*")):
        if file.is_file():
            archive.write(file, Path(source.name) / file.relative_to(source))
print(f"Native title archive: {dest}")
PY

python3 "$project/scripts/native_release_manifest.py" write   "$project/dist/native" "$title" "$pin"
python3 "$project/scripts/native_release_manifest.py" check   "$project/dist/native" "$title" "$pin"
