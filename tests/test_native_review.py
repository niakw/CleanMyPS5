#!/usr/bin/env python3
"""Read-only native capture preview ABI regression in temporary host fixture."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as scratch:
    root=Path(scratch)
    photos=root/"user/av_contents/photo/NPXS00000/GAME123"
    videos=root/"user/av_contents/video/NPXS00000/GAME123"
    photos.mkdir(parents=True)
    videos.mkdir(parents=True)
    mib=1024*1024
    samples=[
        (videos/"movie.mp4",10*mib),
        (photos/"other.jpg",7*mib),
        (photos/"zeta.png",4*mib),
        (photos/"small.jpeg",mib),
    ]
    for file,length in samples:
        with file.open("wb") as stream:
            stream.truncate(length)
        os.utime(file,(1600000000,1600000000))
    (photos/"duplicate.jpg").hardlink_to(photos/"other.jpg")
    try:
        (photos/"injected.png").symlink_to("/data/secrets")
    except OSError:
        pass
    secret=root/"data/homebrew/saves"
    secret.mkdir(parents=True)
    (secret/"private.jpg").write_bytes(b"do not scan as media")
    result=subprocess.run([sys.argv[1],str(root)],
                          check=True,text=True,capture_output=True,timeout=20)
    assert "Media review:" in result.stdout
print("Native review bounded fixture: PASS")
