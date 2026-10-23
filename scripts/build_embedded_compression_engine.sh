#!/usr/bin/env bash
# Embed the GPL-licensed AMPR/LZ4 engine INSIDE the one CleanMyPS5 title.
# No second home-screen app, no upstream app UI, no background downloads on PS5.
set -euo pipefail
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
target=${1:?Need target native assets directory}
sdk=${2:?Need already provisioned PS5_PAYLOAD_SDK directory}
pin=6b3a76a12e504c196848d49e2676047fc2a8f5d4
for program in git make python3 curl xxd sha256sum unzip; do
 command -v "$program" >/dev/null || { echo "Embedded packer needs $program" >&2; exit 2; }
done
test -x "$sdk/bin/prospero-clang" || { echo "PS5 SDK is missing" >&2; exit 2; }
mkdir -p "$target"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
git clone --quiet --filter=blob:none https://github.com/exFAT2AMPR/exfat2ampr.git "$work/engine"
git -C "$work/engine" checkout --quiet --detach "$pin"
test "$(git -C "$work/engine" rev-parse HEAD)" = "$pin" || {
 echo "Upstream engine pin mismatch" >&2; exit 1;
}
python3 "$project/scripts/patch_embedded_engine.py" "$work/engine"
python3 "$project/scripts/verify_embedded_engine_policy.py" "$work/engine"
make -C "$work/engine" deps
# Source commit + release-asset SHA-256 pins: a tagged URL is not by
# itself sufficient to authenticate executable SPRX dependencies.
(
 cd "$work/engine"
 printf '%s  %s\n' \
  '69e6c4d5e4f5fb83c9e01815db5861c4c75734acbf4595cafa50d4c218116d1a' 'third_party/libSceAmpr.sprx' \
  'b44a986f2fa9903e74a34cbbd4681e899cd99196613652cd073ed11f2ca947c2' 'third_party/libSceAmpr_debug.sprx' \
  'f5fdb0ed6cc7413e6dcac79a62e0efd8cfbae04cca0bf932e7a741aed850d213' 'third_party/plugins/libScePlayGo.sprx' \
  'c9f353ae4c7951de57ae3b6f5b3be1683f72866e2137121bb548d061d04f265e' 'third_party/plugins/dlc_emu.zip' \
  | sha256sum --check --strict
)
# Exercise upstream's real LZ4 pack/verify/resume/crash-recovery paths on
# disposable synthetic fixtures. No game data or PS5 is accessed here.
# A packer that merely links is not sufficient for this release.
make -C "$work/engine" -j2 check
make -C "$work/engine" -j2 PS5_PAYLOAD_SDK="$sdk" \
  VERSION="CleanMyPS5-1.0" \
  exfat2ampr.elf
test -s "$work/engine/exfat2ampr.elf"
python3 - "$work/engine/exfat2ampr.elf" <<'PY'
from pathlib import Path
import sys
path=Path(sys.argv[1])
start=path.read_bytes()[:20]
assert start[:4]==bytes.fromhex("7f454c46"), "Engine is not an ELF"
assert start[4:6]==bytes.fromhex("0201"), "Engine is not 64-bit little-endian"
assert int.from_bytes(start[18:20],"little")==62, "Engine is not x86-64"
assert path.stat().st_size<=64*1024*1024, "Unsafe oversized engine"
print("CleanMyPS5 compressed game engine: pinned PS5 x86-64 ELF")
PY
cp "$work/engine/exfat2ampr.elf" "$target/cleanmyps5-compression.elf"
cp "$work/engine/NOTICE" "$target/compression-upstream.NOTICE"
cp "$work/engine/COPYING" "$target/compression-GPL-3.COPYING"
printf '%s\n' "$pin" > "$target/compression-upstream-commit.txt"
printf '%s\n' "Embedded AMPR LZ4 engine, modified by CleanMyPS5 contributors, 2026." \
  "Based on exFAT2AMPR, Copyright (C) 2026 V-10; Made by V-10." \
  "This is a modified version, not endorsed by V-10." \
  "See compression-upstream.NOTICE and compression-GPL-3.COPYING." \
  > "$target/compression-engine-credits.txt"
