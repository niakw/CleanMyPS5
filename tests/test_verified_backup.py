#!/usr/bin/env python3
"""Host-only safety regression; never touches installed PS5 storage."""
from pathlib import Path
import hashlib
import subprocess
import tempfile
import sys

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
    base=Path(temp)
    gallery=base/'user/av_contents/photo/NPXS00000/GAME123'
    gallery.mkdir(parents=True)
    source=gallery/'CAPTURE.JPG'
    data=b'AN ARTIFICIAL CAPTURE - NO PRIVATE CONTENT\n'*5000
    source.write_bytes(data)
    backup=base/'private-backups'
    backup.mkdir(mode=0o700)
    restore=base/'private-restores'
    restore.mkdir(mode=0o700)
    alias=base/'linked-backups'
    alias.symlink_to(backup,target_is_directory=True)
    binary=Path(sys.argv[1]) if len(sys.argv)>1 else base/'backup-test'
    if len(sys.argv)==1:
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror',
                        '-Isrc/native','tests/test_verified_backup.c',
                        'src/native/verified_backup.c','src/native/sha256.c',
                        '-o',str(binary)],cwd=root,check=True,timeout=30)
    completed=subprocess.run([str(binary),str(base),str(source),str(backup),
                              str(restore),'tamper',str(alias)],capture_output=True,text=True,
                              check=False,timeout=30)
    assert completed.returncode==0,(completed.stdout,completed.stderr)
    assert hashlib.sha256(data).hexdigest() in completed.stdout
    assert source.read_bytes()==data
    assert (restore/'SAVED').read_bytes()==data
    assert (backup/'ONE').read_bytes()!=data
    # Failed copies are now retained for audit, never automatically
    # unlinked by a stale pathname after another writer can replace it.
    assert (backup/'.ABORTED.part').is_file()
    assert not (backup/'.ONE.part').exists() # early output collision denial
    assert not (restore/'ANOTHER').exists()
    assert not (backup/'BAD').exists()
    assert not (backup/'ABORTED').exists()
    assert (backup/'.ABORTED.part').is_file()
    assert not (backup/'HARDLINK').exists()
    assert not (backup/'ALIAS_DIR').exists()
    assert not (backup/'SYMLINK').exists()
    assert not (backup/'PUBLIC').exists()
    assert not (backup/'TOO_LARGE').exists()
    assert not source.with_name(source.name+'.link').exists()
    print(completed.stdout.strip())
