#!/usr/bin/env python3
"""Build/check a hash-locked PS5 homebrew release without accessing the console.

The manifest never contains machine paths, personal information, secrets, or
an unsupported claim that a native title folder is a signed retail package.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import sys
from zipfile import ZipFile

REQUIRED_IN_ZIP = (
    "eboot.bin",
    "sce_sys/param.json",
    "sce_sys/icon0.png",
    "sce_module/libc.prx",
)

def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()

def validate_zip(path: Path, title: str) -> None:
    if not path.is_file() or path.is_symlink():
        raise ValueError("Native title archive missing or symlinked")
    with ZipFile(path) as package:
        names: set[str] = set()
        for info in package.infolist():
            name = info.filename
            # No traversal, absolute paths, symlink entries, Windows drive paths,
            # or additional top-level files independent of the native title.
            parts = name.split("/")
            if (
                not name.startswith(title + "/")
                or any(p in ("", ".", "..") for p in parts)
                or "\\" in name
                or ":" in name
                or info.is_dir()
                or (info.external_attr >> 16) & 0o170000 == 0o120000
                or name in names
            ):
                raise ValueError("Unexpected native title archive entry")
            names.add(name)
        for relative in REQUIRED_IN_ZIP:
            name = f"{title}/{relative}"
            if name not in names or package.getinfo(name).file_size == 0:
                raise ValueError(f"Missing native title component: {relative}")
        with package.open(f"{title}/sce_sys/param.json") as stream:
            params = json.load(stream)
        if params.get("titleId") != title:
            raise ValueError("Native title metadata mismatch")

def expected_manifest(folder: Path, title: str, pin: str) -> dict:
    if not re.fullmatch(r"PPSA[0-9]{5}", title):
        raise ValueError("Invalid homebrew title ID")
    if not re.fullmatch(r"[0-9a-f]{40}", pin):
        raise ValueError("Invalid source revision pin")
    archive = folder / f"{title}.zip"
    helper = folder / "cleanmyps5-helper.elf"
    validate_zip(archive, title)
    if not helper.is_file() or helper.is_symlink() or helper.stat().st_size < 64:
        raise ValueError("Separate read-only helper ELF missing")
    pkg = folder / f"{title}.ffpkg"
    if pkg.exists():
        if not pkg.is_file() or pkg.is_symlink() or pkg.stat().st_size < 0x10560:
            raise ValueError("Unsafe native FFPKG output")
        # UFS2 superblock check: same test as the pinned package builder.
        with pkg.open("rb") as stream:
            stream.seek(0x1055C)
            if stream.read(4) != bytes.fromhex("19015419"):
                raise ValueError("Homebrew FFPKG UFS2 superblock missing")
    files = []
    sources=(archive, helper, pkg) if pkg.exists() else (archive, helper)
    for path in sources:
        files.append({
            "name": path.name,
            "bytes": path.stat().st_size,
            "sha256": sha256(path),
        })
    return {
        "schemaVersion": 1,
        "titleId": title,
        "format": "unsigned-homebrew-title-folder",
        "helper": "separate-browser-free-payload",
        "runtimePermission": "not-guaranteed",
        "firmware1360HardwareValidated": False,
        "cleanupEnabled": False,
        "sourceToolchainCommit": pin,
        "files": files,
    }

def write(folder: Path, title: str, pin: str) -> None:
    manifest = expected_manifest(folder, title, pin)
    target = folder / "manifest.json"
    target.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("Native release manifest created (not a PS5 runtime validation)")

def check(folder: Path, title: str, pin: str) -> None:
    target = folder / "manifest.json"
    if not target.is_file() or target.is_symlink():
        raise ValueError("Native release manifest missing")
    manifest = json.loads(target.read_text(encoding="utf-8"))
    if manifest != expected_manifest(folder, title, pin):
        raise ValueError("Native release manifest hash or contract mismatch")
    print("Native title + helper SHA-256 manifest: PASS")

if __name__ == "__main__":
    if len(sys.argv) != 5 or sys.argv[1] not in ("write", "check"):
        raise SystemExit("usage: native_release_manifest.py write|check DIR TITLE_ID TOOLCHAIN_SHA")
    try:
        globals()[sys.argv[1]](Path(sys.argv[2]), sys.argv[3], sys.argv[4])
    except (ValueError, OSError, KeyError, json.JSONDecodeError) as exc:
        raise SystemExit(f"Native release validation failed: {exc}") from exc
