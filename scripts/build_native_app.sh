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
python3 "$project/scripts/make_native_icon.py" "$tmp/native-toolchain/sce_sys/icon0.png"
# Never distribute the toolchain demo title backgrounds/audio.
rm -f -- "$tmp/native-toolchain/sce_sys/pic0.dds" \
            "$tmp/native-toolchain/sce_sys/pic1.dds" \
            "$tmp/native-toolchain/sce_sys/snd0.at9"
rm -f -- "$tmp/native-toolchain/assets/banner.txt"
printf '%s\n' "CleanMyPS5 native UI - backend remains external and read only." \
  > "$tmp/native-toolchain/assets/native-readme.txt"

make -C "$tmp/native-toolchain" init \
  TITLE_ID="$title" APP_NAME=CleanMyPS5 APP_CATEGORY=game
make -C "$tmp/native-toolchain" app

# Keep privileged filesystem inspection in the separately launched payload,
# never in the installed title's sandbox, and never open WebKit.
PS5_PAYLOAD_SDK="$tmp/native-toolchain/.deps/native/ps5-payload-sdk" \
  make -C "$project" ps5-helper

python3 "$project/scripts/check_native_bundle.py" \
  "$tmp/native-toolchain/dist/$title" "$title"

mkdir -p "$project/dist/native"
cp "$project/cleanmyps5-helper.elf" "$project/dist/native/cleanmyps5-helper.elf"
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
