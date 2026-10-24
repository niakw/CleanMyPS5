#!/usr/bin/env python3
"""Atomic multi-member gallery backups, host synthetic fixtures only."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile

repo=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as scratch:
    root=Path(scratch)
    gallery=root/"user/av_contents/photo/NPXS00000/GAME123"
    gallery.mkdir(parents=True)
    contents={
        "REC_001.jxr":(b"JXR-SYNTHETIC-ONLY"*14000),
        "REC_001.jpg":(b"JPEG-SDR-SYNTHETIC-ONLY"*5000),
        "REC_001.dat":(b"\x00"*0x44+b"Demo Game_20261022120000"+b"\x00"*40),
    }
    for name,data in contents.items():
        (gallery/name).write_bytes(data)
    # 2-GiB sparse video: metadata-only inspection must work without
    # allocating disk blocks, reading video bytes or permitting a backup.
    video=root/"user/av_contents/video/NPXS00000/GAME123"
    video.mkdir(parents=True)
    large_video=video/"BIG_001.mp4"
    large_size=2*1024*1024*1024+17
    with large_video.open("wb") as handle:
        handle.truncate(large_size)
    large_sidecar=video/"BIG_001.dat"
    large_sidecar.write_bytes(b"VIDEO-METADATA")
    backup=root/"private-backups"
    restore=root/"private-restores"
    backup.mkdir(mode=0o700)
    restore.mkdir(mode=0o700)
    binary=Path(sys.argv[1]) if len(sys.argv)>1 else root/"verified-group"
    if len(sys.argv)==1:
        subprocess.run([
          "cc","-std=c11","-O1","-Wall","-Wextra","-Wpedantic","-Werror",
          "-Isrc/native","tests/test_verified_group.c",
          "src/native/verified_group.c","src/native/capture_companions.c",
          "src/native/verified_backup.c",
          "src/native/sha256.c","-o",str(binary)
        ],cwd=repo,check=True,timeout=40)
    args=[str(binary),str(root),*(str(gallery/name) for name in contents),
          str(backup),str(restore),str(large_video),str(large_sidecar)]
    result=subprocess.run(args,cwd=repo,check=False,capture_output=True,text=True,timeout=35)
    assert result.returncode==0, (result.returncode,result.stdout,result.stderr)
    assert "multi-file capture group" in result.stdout
    assert large_video.stat().st_size==large_size
    assert large_sidecar.read_bytes()==b"VIDEO-METADATA"
    assert not (backup/"BIGBACKUP__COMMIT").exists()
    for i,(_,data) in enumerate(contents.items(),1):
        dest=restore/f"ALBUM__{i}"
        if i==2:  # Intentionally corrupted AFTER the successful full restore.
            assert dest.read_bytes()!=data
        else:
            assert dest.read_bytes()==data
            assert hashlib.sha256(dest.read_bytes()).hexdigest()==hashlib.sha256(data).hexdigest()
    assert (backup/"ALBUM__COMMIT").stat().st_size==256
    assert (restore/"ALBUM__RESTORED").stat().st_size==256
    assert not (backup/"SINGLE__COMMIT").exists()
    assert not (backup/"MIXED__COMMIT").exists()
    assert not (backup/"ALIAS__COMMIT").exists()
    assert not (backup/"ABORTED__COMMIT").exists()
    assert not (restore/"ANOTHER__RESTORED").exists()
    assert not list(backup.glob("*.part"))
    assert not list(restore.glob("*.part"))
    assert not (backup/"BAD").exists()
    for name,data in contents.items():
        assert (gallery/name).read_bytes()==data, "PS5 gallery test source changed"
    print("Atomic multi-file JXR/JPG/DAT backup and private restore: PASS")
