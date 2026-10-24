#!/usr/bin/env python3
"""Host-only evidence for a known thumbnail path, never a deletion contract."""
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1])
denied_binary = Path(sys.argv[2])
with tempfile.TemporaryDirectory() as work:
    root=Path(work)
    gallery=root/"user/av_contents/photo/NPXS00000/GAME123"
    gallery.mkdir(parents=True)
    original=gallery/"REC_001.jxr"
    original.write_bytes(b"JXR SYNTHETIC CAPTURE")
    (gallery/"REC_001.dat").write_bytes(b"DAT SYNTHETIC SIDECAR")
    thumb=root/"user/av_contents/thumbnails/photo/NPXS00000/GAME123"
    matching=thumb/"REC_001.jpg.jpeg"
    def verify(media,relative,expected):
        result=subprocess.run(
            [str(binary),str(root),str(media),relative,expected],
            capture_output=True,text=True,timeout=10)
        assert result.returncode==0,(expected,result.stdout,result.stderr)
    relative="/user/av_contents/photo/NPXS00000/GAME123/REC_001.jxr"
    verify(original,relative,"missing")
    thumb.mkdir(parents=True)
    matching.write_bytes(b"JPEG-CANDIDATE-ONLY")
    verify(original,relative,"found")
    denied=subprocess.run(
        [str(denied_binary),str(root),str(original),relative],
        capture_output=True,text=True,timeout=10)
    assert denied.returncode==0,(denied.stdout,denied.stderr)
    matching.unlink()
    verify(original,relative,"missing")
    matching.symlink_to(original)
    verify(original,relative,"unsafe")
    matching.unlink()
    matching.mkdir()
    verify(original,relative,"unsafe")
    matching.rmdir()
    # A symlink in an intermediate directory must not be traversed.
    thumb.rmdir()
    thumb.symlink_to(gallery,target_is_directory=True)
    verify(original,relative,"unsafe")
    thumb.unlink()
    # Unreadable root isn't interpreted as an absent thumbnail.
    # Video thumbnail conventions are deliberately outside this contract.
    video=root/"user/av_contents/video/NPXS00000/GAME123"
    video.mkdir(parents=True)
    sample=video/"REC_002.mp4"
    sample.write_bytes(b"VIDEO SYNTHETIC")
    (video/"REC_002.dat").write_bytes(b"DAT SYNTHETIC SIDECAR")
    verify(sample,"/user/av_contents/video/NPXS00000/GAME123/REC_002.mp4",
           "not-checked")
    assert original.read_bytes()==b"JXR SYNTHETIC CAPTURE"
print("Cross-tree JPG.JPEG discovery remains read-only, bounded and unsafe-on-alias: PASS")
