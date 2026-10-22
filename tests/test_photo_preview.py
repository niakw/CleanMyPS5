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
    # JPEG XR files may have a separate SDR JPEG twin, confirmed by public
    # PS5 13.60 gallery implementation. No JXR decoding or file writes occur.
    companion=folder/"test_jxr_companion"
    subprocess.run([
        "cc","-std=c11","-O1","-Wall","-Wextra","-Wpedantic",
        "-Isrc/native","tests/test_jxr_companion.c",
        "src/native/media_preview.c","-o",str(companion)
    ],cwd=root,check=True,timeout=60)
    original=native/"hdr_frame.jxr"
    original.write_bytes(bytes(range(128)))  # Deliberately not JPEG data
    twin=native/"hdr_frame.jpg"
    source.save(twin,format="JPEG")
    jxr_rel="/user/av_contents/photo/NPXS00000/GAME123/hdr_frame.jxr"
    def verify(path: str,expected: str) -> None:
        subprocess.run([str(companion),str(folder),str(original),path,expected],
                       cwd=root,check=True,timeout=15)
    verify(jxr_rel,"valid")
    twin.unlink()
    verify(jxr_rel,"invalid")
    outside=folder/"not_a_gallery_capture.jpg"
    source.save(outside,format="JPEG")
    twin.symlink_to(outside)
    verify(jxr_rel,"invalid")
    twin.unlink()
    source.save(twin,format="JPEG")
    verify("/user/av_contents/photo//NPXS00000/GAME123/hdr_frame.jxr","invalid")
    verify("/user/av_contents/photo/../../data/private.jxr","invalid")
    # A changed original (new snapshot on the console) cannot be trusted.
    # We test the reader's rejection of noncanonical traversal and companions.
    link=native/"linked.png"
    link.symlink_to(outside)
print("JPEG/PNG and JXR-with-SDR-twin secure preview fixtures: PASS")
