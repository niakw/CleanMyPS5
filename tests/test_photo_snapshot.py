#!/usr/bin/env python3
"""One host-only PNG snapshot fixture: no ImageMagick, Pillow or console."""
from pathlib import Path
import subprocess
import sys
import tempfile

assert len(sys.argv)==2
with tempfile.TemporaryDirectory() as temp:
    root=Path(temp)
    gallery=root/"user/av_contents/photo/NPXS00000/GAME123"
    gallery.mkdir(parents=True)
    original=gallery/"sample.png"
    # The metadata-only native snapshot reader validates the PNG magic.
    original.write_bytes(bytes((137,80,78,71,13,10,26,10))+b"safe-read-only-fixture")
    result=subprocess.run([
        sys.argv[1],str(root),str(original),
        "/user/av_contents/photo/NPXS00000/GAME123/sample.png",
    ],check=True,capture_output=True,text=True,timeout=15)
    assert "PASS" in result.stdout
    assert original.read_bytes().endswith(b"safe-read-only-fixture")
    print(result.stdout.strip())
