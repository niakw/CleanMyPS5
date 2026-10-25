#!/usr/bin/env python3
"""Host-only capture/thumbnail parent-path identity race regression."""
from pathlib import Path
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1])
for mode in ("media","thumbnail"):
    with tempfile.TemporaryDirectory() as tmp:
        root=Path(tmp)
        gallery=root/"user/av_contents/photo/NPXS00000/GAME123"
        gallery.mkdir(parents=True)
        original=gallery/"REC_001.jxr"
        original.write_bytes(b"TEST IMAGE SNAPSHOT")
        (gallery/"REC_001.dat").write_bytes(b"TEST SIDECAR")
        thumbnail=root/"user/av_contents/thumbnails/photo/NPXS00000/GAME123"
        thumbnail.mkdir(parents=True)
        (thumbnail/"REC_001.jpg.jpeg").write_bytes(b"TEST THUMBNAIL")
        parent=gallery if mode=="media" else thumbnail
        moved=parent.with_name(parent.name+"-moved")
        cmd=[str(binary),str(root),str(original),
             "/user/av_contents/photo/NPXS00000/GAME123/REC_001.jxr",
             str(parent),str(moved),mode]
        p=subprocess.run(cmd,capture_output=True,text=True,timeout=20)
        assert p.returncode==0,(mode,p.stdout,p.stderr)
        assert "refused: PASS" in p.stdout
        assert (moved/"REC_001.jxr").exists() if mode=="media" else (moved/"REC_001.jpg.jpeg").exists()
        assert parent.is_dir() and not list(parent.iterdir())
print("Gallery parent-path replacement does not publish stale album evidence: PASS")
