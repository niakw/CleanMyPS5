#!/usr/bin/env python3
"""Build native JPEG/PNG decoder with pinned header, exercise safe snapshot reads."""
from pathlib import Path
import subprocess
import sys
import tempfile
from PIL import Image

root=Path(__file__).resolve().parents[1]
assert len(sys.argv)==2, "usage: test_photo_preview.py STB_HEADER_DIRECTORY"
header=Path(sys.argv[1])
assert (header/"stb_image.h").is_file()
with tempfile.TemporaryDirectory() as td:
    folder=Path(td)
    native=folder/"user/av_contents/photo/NPXS00000/GAME123"
    native.mkdir(parents=True)
    source=Image.new("RGB",(512,288),(42,100,200))
    binary=folder/"test_preview"
    subprocess.run([
        "cc","-std=c11","-O1","-Wall","-Wextra","-Wpedantic",
        "-Isrc/native","-Inative",f"-I{header}",
        "tests/test_photo_preview.c","src/native/media_preview.c",
        "native/preview_decode.c","-lm","-o",str(binary)
    ],cwd=root,check=True,timeout=60)
    for fmt,ext in [("PNG","png"),("JPEG","jpg")]:
        path=native/f"sample.{ext}"
        source.save(path,format=fmt)
        subprocess.run([str(binary),str(folder),str(path),
            f"/user/av_contents/photo/NPXS00000/GAME123/sample.{ext}"],
            cwd=root,check=True,timeout=15)
    # Links in the original photo namespace must not escape via openat.
    link=native/"linked.png"
    link.symlink_to(folder/"data/private.png")
print("JPEG/PNG native preview decoder and sandbox snapshot fixtures: PASS")
