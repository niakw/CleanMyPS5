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
    # A replacement after opening the root must not mix capture trees.
    original=root/"initial"
    backup=root/"moved-original"
    replacement=root/"replacement"
    for parent,title in ((original,"ORIGINAL"),(replacement,"FOREIGN")):
        gallery=parent/f"user/av_contents/photo/NPXS00000/{title}"
        gallery.mkdir(parents=True)
        (gallery/"capture.jpg").write_bytes(b"JPEG TEST DATA")
    moved=subprocess.run([sys.argv[1],str(original),str(backup),
                          str(replacement),"swap"],
                         text=True,capture_output=True,timeout=15)
    assert moved.returncode==0,moved.stdout+"\n"+moved.stderr
    assert "retained initial root" in moved.stdout
    assert (backup/"user/av_contents/photo/NPXS00000/ORIGINAL/capture.jpg").exists()
    assert (original/"user/av_contents/photo/NPXS00000/FOREIGN/capture.jpg").exists()
print("Gallery .dat mutation and anchored root replacement fixtures: PASS")
