#!/usr/bin/env python3
"""Synthetic game paths only, no real PS5 files."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    games=root/"data/homebrew"
    games.mkdir(parents=True)
    (games/"HEALTHY.exfat").write_bytes(b"synthetic game image")
    independent=root/"mnt/usb0/homebrew"
    independent.mkdir(parents=True)
    (independent/"SURVIVOR.exfat").write_bytes(b"surviving external image")
    (games/"FAULT_ENTRY.txt").write_bytes(b"unrecognized")
    folder=games/"FAULT_GAME"
    (folder/"sce_sys").mkdir(parents=True)
    (folder/"eboot.bin").write_bytes(b"synthetic eboot")
    assets=folder/"assets"
    assets.mkdir()
    (assets/"texture.bin").write_bytes(b"synthetic texture")
    for mode in ("normal","stat","dir","regular-inode","regular-size",
                 "regular-mtime","child-inode","child-mtime",
                 "root-mtime","root-replaced","root-eof-error",
                 "regular-device","child-device"):
        result=subprocess.run([sys.argv[1],str(root),mode],
                              capture_output=True,text=True,timeout=12)
        assert result.returncode==0,(mode,result.stdout,result.stderr)
        assert "PASS" in result.stdout
    # Symlinked application/game directory: never follow a second tree,
    # but report that the root inventory cannot prove complete coverage.
    outside=root/"uninspected-private-game"
    (outside/"sce_sys").mkdir(parents=True)
    (outside/"eboot.bin").write_bytes(b"PRIVATE SYNTHETIC GAME")
    alias=games/"ALIAS_GAME"
    alias.symlink_to(outside,target_is_directory=True)
    result=subprocess.run([sys.argv[1],str(root),"game-alias"],
                          capture_output=True,text=True,timeout=12)
    assert result.returncode==0,(result.stdout,result.stderr)
    assert "PASS" in result.stdout
    assert alias.is_symlink()
    assert (outside/"eboot.bin").read_bytes()==b"PRIVATE SYNTHETIC GAME"
print("Game metadata faults, mount exclusions and symlink aliases: PASS")
