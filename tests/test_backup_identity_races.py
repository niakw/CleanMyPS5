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
    # Backup destination may be renamed and replaced during the copy.
    moved_workspace=Path(temp)/"private-backup-during-copy"
    run("backup-workspace-swap",moved_workspace)
    assert moved_workspace.is_dir() and not (moved_workspace/"SWAPPED").exists()
    assert backup.is_dir() and not (backup/"SWAPPED").exists()
    backup.rmdir()
    moved_workspace.rename(backup)
    # The published capsule must be re-opened and hashed, not assumed
    # correct from the staging stream alone.
    run("backup-stage-swapped",restored)
    assert (backup/".INJECT.part").read_bytes()==b"UNRELATED PRIVATE FILE - NEVER DELETE"
    assert (backup/".INJECT.original").is_file()
    assert not (backup/"INJECT").exists()
    run("backup-publish-tamper",restored)
    assert (backup/"BROKEN").is_file()
    run("fsync-backup",restored)
    assert (backup/"UNSYNCED").is_file()
    run("create",restored)
    assert (backup/"GOOD").is_file()
    moved_backup=backup/"GOOD-MOVED"
    run("verify-rename",moved_backup)
    assert moved_backup.is_file() and not (backup/"GOOD").exists()
    moved_backup.rename(backup/"GOOD")
    replaced_workspace=Path(temp)/"private-backup-relocated"
    run("verify-workspace-swap",replaced_workspace)
    assert replaced_workspace.is_dir() and (replaced_workspace/"GOOD").is_file()
    assert backup.is_dir() and not (backup/"GOOD").exists()
    backup.rmdir()
    replaced_workspace.rename(backup)
    run("restore-rename",moved_backup)
    # The rename is real; no successful result or completed restored file.
    assert moved_backup.is_file() and not (restored/"RENAMED").exists()
    assert (restored/".RENAMED.part").exists()
    moved_backup.rename(backup/"GOOD")
    # Restore must reject both SOURCE and DESTINATION directory replacement
    # during an otherwise valid copy, without publishing a success receipt.
    swapped_backup=Path(temp)/"restore-backup-relocated"
    run("restore-backup-workspace-swap",swapped_backup)
    assert swapped_backup.is_dir() and (swapped_backup/"GOOD").is_file()
    assert not (restored/"BACKUPSWAP").exists()
    assert (restored/".BACKUPSWAP.part").exists()
    backup.rmdir()
    swapped_backup.rename(backup)
    swapped_target=Path(temp)/"restore-target-relocated"
    run("restore-target-workspace-swap",swapped_target)
    assert swapped_target.is_dir() and not (swapped_target/"TARGETSWAP").exists()
    assert (swapped_target/".TARGETSWAP.part").exists()
    assert restored.is_dir() and not (restored/"TARGETSWAP").exists()
    restored.rmdir()
    swapped_target.rename(restored)
    # A target altered just after the atomic name publication must not be
    # certified even if the staging stream was hashed correctly.
    run("restore-stage-swapped",restored)
    assert (restored/".RESTAGE.part").read_bytes()==b"UNRELATED PRIVATE FILE - NEVER DELETE"
    assert (restored/".RESTAGE.original").is_file()
    assert not (restored/"RESTAGE").exists()
    run("restore-publish-tamper",restored)
    assert (restored/"CORRUPT").is_file()
    assert (restored/"CORRUPT").read_bytes()!=original
    run("same-workspace",backup/".")
    assert not (backup/"SAVED").exists()
    run("fsync-restore",restored)
    # A failed durability barrier may leave a published private file,
    # but the API must never return a success receipt.
    assert (restored/"UNSYNCED").is_file()
    assert source.read_bytes()==original
print("Private backup source/verification/restore races and workspace alias: PASS")
