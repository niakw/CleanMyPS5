#!/usr/bin/env python3
"""Real two-process, independent group transactions under a private root."""
from pathlib import Path
import subprocess
import sys
import tempfile
binary=Path(sys.argv[1])
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    gallery=root/"user/av_contents/photo/NPXS00000/GAME123"
    gallery.mkdir(parents=True)
    jpg=gallery/"REC_001.jpg"
    dat=gallery/"REC_001.dat"
    jpg.write_bytes(b"CMP5 GROUP PHOTO SYNTHETIC"*7000)
    dat.write_bytes(b"CMP5 GROUP METADATA SYNTHETIC"*20)
    backup=root/"private-groups"
    restore=root/"private-restores"
    backup.mkdir(mode=0o700)
    restore.mkdir(mode=0o700)
    run=subprocess.run(
      [str(binary),str(root),str(jpg),str(dat),str(backup),str(restore)],
      capture_output=True,text=True,timeout=45)
    assert run.returncode==0,(run.stdout,run.stderr)
    assert "contention and release PASS" in run.stdout
    assert jpg.read_bytes()==b"CMP5 GROUP PHOTO SYNTHETIC"*7000
    assert dat.read_bytes()==b"CMP5 GROUP METADATA SYNTHETIC"*20
    for ident in ("BACKA","BACKB"):
        assert (backup/f"{ident}__COMMIT").stat().st_size==256
        assert (restore/f"{ident}__RESTORED").stat().st_size==256
    assert (backup/"BACKC__COMMIT").stat().st_size==256
    assert not list(backup.glob("*.part"))
    assert not list(restore.glob("*.part"))
print("Group transaction locks prevent overlapping backup/restore: PASS")
