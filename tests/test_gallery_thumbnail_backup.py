#!/usr/bin/env python3
"""Host-only thumbnail-inclusive backup and separate private restore."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as base:
    root=Path(base)
    media=root/"user/av_contents/photo/NPXS00000/GAME123"
    thumbdir=root/"user/av_contents/thumbnails/photo/NPXS00000/GAME123"
    media.mkdir(parents=True)
    thumbdir.mkdir(parents=True)
    photo=media/"REC_001.jpg"
    dat=media/"REC_001.dat"
    thumb=thumbdir/"REC_001.jpg.jpeg"
    photo.write_bytes(b"PHOTO-EXAMPLE-NOT-USER-MEDIA"*1800)
    dat.write_bytes(b"SYNTHETIC-PS5-DAT"*17)
    thumb.write_bytes(b"THUMBNAIL-EXAMPLE"*290)
    backup=root/"private-backup"
    restore=root/"private-restore"
    backup.mkdir(mode=0o700)
    restore.mkdir(mode=0o700)
    command=[str(Path(sys.argv[1])),str(root),str(photo),str(dat),
             str(thumb),str(backup),str(restore)]
    result=subprocess.run(command,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,(result.stdout,result.stderr)
    assert "private SHA-256 backup and restore PASS" in result.stdout
    assert (backup/"THUMB__COMMIT").stat().st_size==256
    assert (restore/"THUMB__RESTORED").stat().st_size==256
    assert not (backup/"OMITTED__COMMIT").exists()
    assert not list(backup.glob("*.part"))
    assert not list(restore.glob("*.part"))
    assert thumb.read_bytes()==b"THUMBNAIL-EXAMPLE"*290
    assert photo.read_bytes()==b"PHOTO-EXAMPLE-NOT-USER-MEDIA"*1800
print("Private-only thumbnail-inclusive gallery backup fixture: PASS")
