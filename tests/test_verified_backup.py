#!/usr/bin/env python3
"""Host-only safety regression; never touches installed PS5 storage."""
from pathlib import Path
import hashlib
import subprocess
import tempfile

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
    binary=base/'backup-test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror',
                    '-Isrc/native','tests/test_verified_backup.c',
                    'src/native/verified_backup.c','src/native/sha256.c',
                    '-o',str(binary)],cwd=root,check=True,timeout=30)
    completed=subprocess.run([str(binary),str(base),str(source),str(backup),
                              str(restore),'tamper'],capture_output=True,text=True,
                              check=True,timeout=30)
    assert hashlib.sha256(data).hexdigest() in completed.stdout
    assert source.read_bytes()==data
    assert (restore/'SAVED').read_bytes()==data
    assert (backup/'ONE').read_bytes()!=data
    assert not list(backup.glob('*.part'))
    assert not (restore/'ANOTHER').exists()
    assert not (backup/'BAD').exists()
    print(completed.stdout.strip())
