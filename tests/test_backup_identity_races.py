#!/usr/bin/env python3
"""Host-only rename and alias regression for verified private backup files."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as temp:
    root=Path(temp)/"console-fixture"
    gallery=root/"user/av_contents/photo/NPXS00000/GAME123"
    gallery.mkdir(parents=True)
    source=gallery/"CAPTURE.JPG"
    original=b"SYNTHETIC_NO_REAL_MEDIA"*18000
    source.write_bytes(original)
    backup=Path(temp)/"private-backup"
    restored=Path(temp)/"private-restore"
    backup.mkdir(mode=0o700)
    restored.mkdir(mode=0o700)
    binary=Path(sys.argv[1])
    def run(action,target):
        p=subprocess.run([str(binary),str(root),str(source),str(backup),
            str(restored),action,str(target)],capture_output=True,text=True,timeout=15)
        assert p.returncode==0,(action,p.stdout,p.stderr)
    moved_capture=gallery/"CAPTURE-MOVED.JPG"
    run("backup-rename",moved_capture)
    assert moved_capture.read_bytes()==original and not (backup/"RACED").exists()
    moved_capture.rename(source)
    run("create",restored)
    assert (backup/"GOOD").is_file()
    moved_backup=backup/"GOOD-MOVED"
    run("verify-rename",moved_backup)
    assert moved_backup.is_file() and not (backup/"GOOD").exists()
    moved_backup.rename(backup/"GOOD")
    run("restore-rename",moved_backup)
    # The rename is real; no successful result or completed restored file.
    assert moved_backup.is_file() and not (restored/"SAVED").exists()
    moved_backup.rename(backup/"GOOD")
    run("same-workspace",backup/".")
    assert not (backup/"SAVED").exists()
    assert source.read_bytes()==original
print("Private backup source/verification/restore races and workspace alias: PASS")
