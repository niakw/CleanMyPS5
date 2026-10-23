#!/usr/bin/env python3
"""Test in-place metadata mutation during the real read-only Gallery scanner."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    folder=root/"user/av_contents/photo/NPXS00000/GAME123"
    folder.mkdir(parents=True)
    sidecar=folder/"sample.dat"
    sidecar.write_bytes(b"\x00"*0x44+b"Synthetic Game_20261022105000\x00")
    (folder/"sample.jpg").write_bytes(b"JPEG TEST DATA")
    completed=subprocess.run([sys.argv[1],str(root),str(sidecar)],
                             text=True,capture_output=True,timeout=15)
    assert completed.returncode==0,completed.stdout+"\n"+completed.stderr
    assert "blocked" in completed.stdout
print("Gallery .dat mutation fails closed before UI publication: PASS")
