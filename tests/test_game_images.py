#!/usr/bin/env python3
"""Only bounded synthetic game image metadata; never manipulate actual titles."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    internal=root/"data/homebrew"
    external=root/"mnt/usb0/homebrew"
    top=root/"mnt/ext0"
    for path in (internal,external,top):
        path.mkdir(parents=True)
    with (internal/"GAME.exfat").open("wb") as f:f.truncate(21*1024**3)
    with (external/"GAME2.EXFAT").open("wb") as f:f.truncate(10*1024**3)
    (external/"GAME3.ffpkg").write_bytes(b"a"*120)
    (top/"EXPERIMENTAL.ffpfs").write_bytes(b"b"*340)
    (top/"COMPRESSED.ffpfsc").write_bytes(b"c"*560)
    eta=root/"mnt/usb1/etaHEN/games"
    eta.mkdir(parents=True)
    with (eta/"EAGAME.exfat").open("wb") as f:f.truncate(24*1024**3)
    game=internal/"PPSA12345"
    (game/"sce_sys").mkdir(parents=True)
    (game/"eboot.bin").write_bytes(b"SYNTHETIC EBOOT")
    (game/"sce_sys/param.json").write_bytes(b"{}")
    with (game/"data.bin").open("wb") as f:f.truncate(22*1024**3)
    (external/"LINK.ffpkg").hardlink_to(external/"GAME3.ffpkg")
    (external/"SYMLINK.exfat").symlink_to(internal/"GAME.exfat")
    # Extension detection is not proof that any image really contains a game.
    # A synthetic 23 GiB file is used to exercise the large-size threshold.
    with (top/"LARGE.ffpkg").open("wb") as f:f.truncate(23*1024**3)
    result=subprocess.run([sys.argv[1],str(root)],capture_output=True,text=True,timeout=20)
    assert result.returncode==0,(result.stdout,result.stderr)
    assert "PASS" in result.stdout
print("Game image bounded metadata and alias exclusions: PASS")
