#!/usr/bin/env python3
"""Native backup-only group regression. Never reads or writes a real PS5."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="cmp5-native-group-") as temp:
    root = Path(temp)
    photos = root / "user/av_contents/photo/NPXS00000/GAME123"
    thumbnails = root / "user/av_contents/thumbnails/photo/NPXS00000/GAME123"
    photos.mkdir(parents=True)
    thumbnails.mkdir(parents=True)
    photo = photos / "REC_001.jpg"
    dat = photos / "REC_001.dat"
    thumb = thumbnails / "REC_001.jpg.jpeg"
    photo.write_bytes(b"NATIVE-VISIBLE-PHOTO" * 2048)
    dat.write_bytes(b"NATIVE-VISIBLE-METADATA" * 12)
    thumb.write_bytes(b"NATIVE-KNOWN-THUMBNAIL" * 128)
    original = {path: hashlib.sha256(path.read_bytes()).hexdigest()
                for path in (photo, dat, thumb)}
    target = root / "private-backup"
    target.mkdir(mode=0o700)
    binary = Path(sys.argv[1])
    result = subprocess.run((str(binary), str(root), str(photo), str(target)),
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert "Native-mode additive group backup" in result.stdout
    assert (target / "NATIVE_GROUP__COMMIT").stat().st_size == 256
    assert (target / "NATIVE_GROUP__ORIGINS").is_file()
    assert len(list(target.glob("NATIVE_GROUP__*"))) == 5
    assert not list(target.glob("*.part"))
    for path, digest in original.items():
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest
    symbols = subprocess.run(("nm", "-g", str(binary)),
                             check=True, capture_output=True, text=True).stdout
    forbidden = ("cmp5_restore_group", "cmp5_restore_backup",
                 "cmp5_verify_restored_group",
                 "cmp5_inspect_backup_restore_pair",
                 "cmp5_inspect_restored_group_recovery")
    assert not any(name in symbols for name in forbidden), symbols[-5000:]
print("Native-mode private group backup: no restore symbols, originals intact: PASS")
