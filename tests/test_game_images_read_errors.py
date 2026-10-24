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
    (games/"FAULT_ENTRY.txt").write_bytes(b"unrecognized")
    folder=games/"FAULT_GAME"
    (folder/"sce_sys").mkdir(parents=True)
    (folder/"eboot.bin").write_bytes(b"synthetic eboot")
    for mode in ("normal","stat","dir"):
        result=subprocess.run([sys.argv[1],str(root),mode],
                              capture_output=True,text=True,timeout=12)
        assert result.returncode==0,(mode,result.stdout,result.stderr)
        assert "PASS" in result.stdout
print("Game metadata EIO and directory EACCES fixtures: PASS")
