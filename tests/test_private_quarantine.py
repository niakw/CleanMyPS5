#!/usr/bin/env python3
"""Host-only test. All writes are confined to isolated temporary directories."""
from pathlib import Path
import subprocess
import sys
import tempfile
binary=Path(sys.argv[1])
with tempfile.TemporaryDirectory() as tmp:
    base=Path(tmp)
    gallery=base/"user/av_contents/photo/NPXS00000/GAME123"
    gallery.mkdir(parents=True)
    item=gallery/"DEMO.JPG"
    contents=b"CMP5 SYNTHETIC GALLERY DATA ONLY "*128
    item.write_bytes(contents)
    lab=base/"cmp5-quarantine-lab"
    backup=base/"private-capsules"
    rehearsal=base/"private-rehearsal"
    unsafe=base/"ordinary-workspace"
    for d in (lab,backup,rehearsal,unsafe):d.mkdir(mode=0o700)
    (lab/"ITEM").write_bytes(contents)
    (lab/"ITEM").chmod(0o600)
    args=[str(binary),str(base),str(item),str(lab),str(backup),
          str(rehearsal),str(unsafe)]
    result=subprocess.run(args,capture_output=True,text=True,timeout=35)
    assert result.returncode==0,(result.stdout,result.stderr)
    assert "Private lab backup" in result.stdout
    assert item.read_bytes()==contents
    assert (rehearsal/"TRIAL").read_bytes()==contents
    assert (lab/"ITEM").read_bytes()!=contents # intentionally modified at end
    assert not (lab/".ITEM.CMP5_HELD").exists()
    assert not list(lab.glob("*.part"))
    assert not (unsafe/".ITEM.CMP5_HELD").exists()
print("Host-only reversible quarantine laboratory: PASS")
