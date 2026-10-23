#!/usr/bin/env python3
"""Synthetic release package fixtures; never touches real PS5 directories."""
import importlib.util
import json
from pathlib import Path
import tempfile
from zipfile import ZipFile, ZipInfo

spec = importlib.util.spec_from_file_location(
    "native_release_manifest", Path(__file__).resolve().parents[1] / "scripts/native_release_manifest.py"
)
module = importlib.util.module_from_spec(spec)
# Fast fail before the expensive PS5 SDK build if the title's overlay
# omits the read-only game scanner's local dependency.
build_script = (Path(__file__).resolve().parents[1] /
                "scripts/build_native_app.sh").read_text()
assert ('cp "$project/src/native/game_scan_identity.h" ' +
        '"$tmp/native-toolchain/src/game_scan_identity.h"') in build_script
# The pinned PS5 native libc has no dirfd symbol. Never accidentally
# reintroduce it through the game scanner after host tests pass.
scanner_source = (Path(__file__).resolve().parents[1] /
                  "src/native/game_images.c").read_text()
assert "dirfd(d)" not in scanner_source.split("static int total_game_tree")[1].split("static int safe_label")[0]
assert spec.loader is not None
spec.loader.exec_module(module)

title = "PPSA99784"
pin = "a" * 40

def package(folder: Path, *, traversal: bool = False, wrong_id: bool = False,
            duplicate: bool = False, symlink: bool = False) -> None:
    with ZipFile(folder / f"{title}.zip", "w") as z:
        params = {"titleId": "PPSA00000" if wrong_id else title}
        z.writestr(f"{title}/sce_sys/param.json", json.dumps(params))
        z.writestr(f"{title}/eboot.bin", b"\x4f\x15\x3d\x1d" + b"A" * 80)
        z.writestr(f"{title}/sce_sys/icon0.png", b"\x89PNG\r\n\x1a\n")
        z.writestr(f"{title}/sce_module/libc.prx", b"B" * 100)
        if traversal:
            z.writestr(f"{title}/../../escape.txt", "bad")
        if duplicate:
            z.writestr(f"{title}/eboot.bin", b"duplicated")
        if symlink:
            link = ZipInfo(f"{title}/assets/link")
            link.create_system = 3
            link.external_attr = (0o120777 << 16)
            z.writestr(link, "some-file")

with tempfile.TemporaryDirectory() as root:
    folder = Path(root)
    package(folder)
    helper = folder / "cleanmyps5-helper.elf"
    helper.write_bytes(b"\x7fELF" + b"R" * 128)
    module.write(folder, title, pin)
    module.check(folder, title, pin)
    manifest = json.loads((folder / "manifest.json").read_text())
    assert len(manifest["files"]) == 2
    assert manifest["cleanupEnabled"] is False
    assert manifest["firmware1360HardwareValidated"] is False
    assert all("sha256" in item for item in manifest["files"])
    helper.write_bytes(helper.read_bytes() + b"X")
    try:
        module.check(folder, title, pin)
        raise AssertionError("Changed helper must invalidate SHA-256 manifest")
    except ValueError:
        pass
    helper.write_bytes(helper.read_bytes()[:-1])

    for args in ({"traversal": True}, {"wrong_id": True},
                 {"duplicate": True}, {"symlink": True}):
        package(folder, **args)
        try:
            module.expected_manifest(folder, title, pin)
            raise AssertionError(f"Unsafe package accepted: {args}")
        except ValueError:
            pass

    # Optional native UFS2 homebrew installation package is checked separately.
    package(folder)
    pkg=folder/f"{title}.ffpkg"
    with pkg.open("wb") as stream:
        stream.seek(0x1055C)
        stream.write(bytes.fromhex("19015419"))
    module.write(folder,title,pin)
    module.check(folder,title,pin)
    with (folder/"manifest.json").open() as stream:
        assert len(json.load(stream)["files"])==3
    with pkg.open("r+b") as stream:
        stream.seek(0x1055C)
        stream.write(b"BAD!")
    try:
        module.check(folder,title,pin)
        raise AssertionError("Corrupted UFS2 package was accepted")
    except ValueError:
        pass
    pkg.unlink()

print("Native release integrity and zip path validation: PASS")
