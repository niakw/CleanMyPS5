#!/usr/bin/env python3
"""Validate that the output is a PS5 native title, not a renamed payload."""
from __future__ import annotations

import json
from pathlib import Path
import re
import sys

REQUIRED = ("eboot.bin", "sce_sys/icon0.png", "sce_sys/param.json", "sce_module/libc.prx")

def validate(base: Path, expected_id: str) -> None:
    if not re.fullmatch(r"PPSA[0-9]{5}", expected_id):
        raise SystemExit("Invalid expected title ID")
    if not base.is_dir() or base.name != expected_id:
        raise SystemExit("Native title folder missing or wrong ID")
    for relative in REQUIRED:
        file = base / relative
        if not file.is_file() or file.stat().st_size == 0:
            raise SystemExit("Missing or empty native title file: " + relative)
    manifest = json.loads((base / "sce_sys/param.json").read_text())
    if manifest.get("titleId") != expected_id:
        raise SystemExit("Title ID does not match title folder")
    if "CLEANMYPS5" not in manifest.get("contentId", ""):
        raise SystemExit("Incorrect application identity")
    title = manifest.get("localizedParameters", {}).get(
        manifest.get("localizedParameters", {}).get("defaultLanguage", ""), {}
    ).get("titleName")
    if title != "CleanMyPS5":
        raise SystemExit("Wrong native application title")
    header = (base / "eboot.bin").read_bytes()[:4]
    if header not in (bytes.fromhex("4f153d1d"), bytes.fromhex("5441f5ee")):
        raise SystemExit("Output is not the expected signed development SELF container")
    icon = (base / "sce_sys/icon0.png").read_bytes()[:24]
    if not (icon.startswith(b"\x89PNG\r\n\x1a\n") and
            icon[16:24] == bytes.fromhex("0000020000000200")):
        raise SystemExit("Native launcher icon is not 512 x 512")
    if (base / "sce_sys/pic0.dds").exists() or (base / "sce_sys/pic1.dds").exists():
        raise SystemExit("Unexpected template artwork was packaged")
    if not (base / "assets/native-readme.txt").is_file():
        raise SystemExit("Application assets missing")
    helper = base / "assets/cleanmyps5-helper.elf"
    if not helper.is_file():
        raise SystemExit("Missing bundled read-only diagnostic helper")
    with helper.open("rb") as handle:
        probe = handle.read(20)
    if (probe[:6] != bytes.fromhex("7f454c460201")
            or int.from_bytes(probe[18:20], "little") != 62
            or not 4096 <= helper.stat().st_size <= 64 * 1024 * 1024):
        raise SystemExit("Bundled diagnostic helper is not x86-64 ELF")
    # This is a real ELF packer included in this SAME installable homebrew,
    # not a promise that a separately installed title provides compression.
    embedded = base / "assets/cleanmyps5-compression.elf"
    if not embedded.is_file():
        raise SystemExit("Missing embedded LZ4 compression engine")
    with embedded.open("rb") as stream:
        header = stream.read(20)
    if (header[:6] != bytes.fromhex("7f454c460201")
            or int.from_bytes(header[18:20], "little") != 62
            or not 4096 <= embedded.stat().st_size <= 64 * 1024 * 1024):
        raise SystemExit("Bundled compression engine is not valid x86-64 ELF")
    for filename in ("compression-upstream.NOTICE",
                     "compression-GPL-3.COPYING",
                     "compression-engine-credits.txt",
                     "compression-upstream-commit.txt"):
        if not (base / "assets" / filename).is_file():
            raise SystemExit("Missing bundled upstream notice: " + filename)
    credits = (base / "assets/compression-engine-credits.txt").read_text()
    if "V-10" not in credits or "modified" not in credits.lower():
        raise SystemExit("Embedded engine attribution is missing")
    pin = (base / "assets/compression-upstream-commit.txt").read_text().strip()
    if pin != "6b3a76a12e504c196848d49e2676047fc2a8f5d4":
        raise SystemExit("Embedded engine source pin does not match")
    print("Native PS5 title structure: PASS (hardware not validated)")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: check_native_bundle.py DIRECTORY TITLE_ID")
    validate(Path(sys.argv[1]), sys.argv[2])
