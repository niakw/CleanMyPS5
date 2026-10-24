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
    # PS5 13.60 capture .dat stores the game name starting at offset 0x44.
    (photos/"capture_meta.dat").write_bytes(
        b"\x00"*0x44+b"Astro's Playroom_20261022105000\x00"
    )
    # Never follow a local metadata symlink, even to a plausible file.
    (videos/"alias.dat").symlink_to(photos/"capture_meta.dat")
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
    # Malformed metadata is a recoverable local failure: show both photos
    # and videos that remain readable, but label the whole scan partial.
    damaged_root=root/"damaged-sidecar-scan"
    damaged_photo=damaged_root/"user/av_contents/photo/NPXS00000/BADGAME"
    damaged_video=damaged_root/"user/av_contents/video/NPXS00000/BADGAME"
    damaged_photo.mkdir(parents=True)
    damaged_video.mkdir(parents=True)
    (damaged_photo/"early_corrupt.dat").write_bytes(b"BAD")
    (damaged_photo/"still_visible_1.jpg").write_bytes(b"A"*1024)
    (damaged_photo/"still_visible_2.png").write_bytes(b"B"*2048)
    (damaged_video/"still_visible_3.mp4").write_bytes(b"C"*3072)
    damaged_result=subprocess.run(
        [sys.argv[1],str(damaged_root),"damaged-sidecar"],
        check=True,text=True,capture_output=True,timeout=20
    )
    assert "damaged sidecar does not conceal healthy captures" in damaged_result.stdout
    # The display-name cache is intentionally capped at 128 distinct
    # titles. The 129th must report PARTIAL, never silently lose labels
    # while asserting a complete catalog.
    title_root=root/"many-gallery-titles"
    title_photo=title_root/"user/av_contents/photo/NPXS00000"
    title_video=title_root/"user/av_contents/video"
    title_video.mkdir(parents=True)
    for i in range(130):
        name_dir=title_photo/f"GAME{i:05d}"
        name_dir.mkdir(parents=True)
        (name_dir/"caption.dat").write_bytes(
            b"\x00"*0x44+f"Game {i:05d}".encode("ascii")+b"\x00"
        )
    cap_result=subprocess.run(
        [sys.argv[1],str(title_root),"sidecar-cap"],
        check=True,text=True,capture_output=True,timeout=20
    )
    assert "sidecar title limit signals incomplete coverage" in cap_result.stdout
    # A fresh workspace with NONE of the configured known roots is not a
    # successful inventory with zero captures. Assert "partial" on macOS
    # and Linux, with no dependence on system permissions.
    empty_root=root/"empty-scan-workspace"
    empty_root.mkdir()
    empty_result=subprocess.run([sys.argv[1],str(empty_root),"empty"],
                                check=True,text=True,capture_output=True,timeout=20)
    assert "all roots absent means partial" in empty_result.stdout
    # Critical: /data is readable and inventories normally, but both Gallery
    # capture roots are invisible. A zero-capture result is NOT complete.
    app_only=root/"app-visible-no-gallery"
    app_dir=app_only/"data/SomeHomebrew"
    app_dir.mkdir(parents=True)
    (app_dir/"cache.log").write_bytes(b"TEST-ONLY")
    app_result=subprocess.run([sys.argv[1],str(app_only),"no-gallery"],
                              check=True,text=True,capture_output=True,timeout=20)
    assert "readable app root cannot mask missing Gallery" in app_result.stdout
    # Linux supports creating the >2-KiB source path needed to reach the
    # scanner's own 2048-byte budget. macOS's stricter system path ceiling
    # prevents constructing this specific fixture before the scanner runs.
    if sys.platform.startswith("linux"):
        deep=photos
        levels=[]
        for i in range(10):
            deep=deep/("LONG_"+str(i).zfill(2)+"_"+"x"*210)
            deep.mkdir()
            levels.append(deep)
        overlong=subprocess.run([sys.argv[1],str(root),"longpath"],
                                text=True,capture_output=True,timeout=20)
        assert overlong.returncode==0, (
            f"longpath regression: {overlong.returncode}\n"
            f"stdout={overlong.stdout}\nstderr={overlong.stderr}"
        )
        assert "overlong path reports incomplete coverage" in overlong.stdout
        for folder in reversed(levels):
            folder.rmdir()
    # Regression: media number now follows scan cap, not the former top-12.
    for i in range(60):
        file=photos / f"capture_{i:03d}.jpg"
        with file.open("wb") as stream:
            stream.truncate(1024*(i+1))
    result=subprocess.run([sys.argv[1],str(root),"64"],
                          check=True,text=True,capture_output=True,timeout=20)
    assert "all catalog entries retained" in result.stdout
    # Stress a moderately sized gallery while retaining hardlink skipping,
    # exact inode identity and the 2,500-entry per-root traversal limit.
    for i in range(512):
        file=photos / f"large_catalog_{i:04d}.jpg"
        with file.open("wb") as stream:
            stream.truncate(1024*(i+1))
    result=subprocess.run([sys.argv[1],str(root),"576"],
                          check=True,text=True,capture_output=True,timeout=20)
    assert "all catalog entries retained" in result.stdout
    cancelled=subprocess.run([sys.argv[1],str(root),"cancel"],
                             check=True,text=True,capture_output=True,timeout=20)
    assert "Metadata scan cancellation" in cancelled.stdout
print("Native review bounded fixture: PASS")
