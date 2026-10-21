#!/usr/bin/env python3
"""Bounded scanner regression on fixtures; no console access."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as t:
    root=Path(t)
    photo=root/"user/av_contents/photo/NPXS40087/GAME"
    video=root/"user/av_contents/video/NPXS40087/GAME"
    eta=root/"data/etaHEN"
    game=root/"data/prosperoeden"
    for p in (photo,video,eta,game):p.mkdir(parents=True)
    (photo/"a.png").write_bytes(b"a"*30)
    (video/"b.MP4").write_bytes(b"b"*200)
    (eta/"debug.log").write_bytes(b"log")
    (eta/"config.ini").write_text("protected")
    (game/"keys.bin").write_text("never include")
    (photo/"other.dat").write_text("metadata")
    try:(photo/"link.png").symlink_to(game/"keys.bin")
    except OSError:pass
    result=subprocess.run([sys.argv[1],"--root",str(root)],text=True,capture_output=True,check=True)
    data=json.loads(result.stdout)
    assert data["schemaVersion"]==1
    names={Path(x["path"]).name:x for x in data["items"]}
    assert set(names)=={"a.png","b.MP4","debug.log","config.ini"},names
    assert names["a.png"]["bytes"]==30
    assert names["b.MP4"]["bytes"]==200
    assert names["config.ini"]["risk"]=="protected"
    assert (game/"keys.bin").read_text()=="never include"
    assert (eta/"config.ini").read_text()=="protected"
    assert all(x["risk"]!="safe" for x in data["items"])
    assert subprocess.run([sys.argv[1],"--root",str(root/"absent")],capture_output=True).returncode==2
print("Scanner read-only fixture: OK")
