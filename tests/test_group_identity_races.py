#!/usr/bin/env python3
"""No real console files are read: full host-fixture race against private group files."""
from pathlib import Path
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory() as temp:
    workspace=Path(temp)/"private"
    workspace.mkdir(mode=0o700)
    result=subprocess.run([sys.argv[1],str(workspace)],
                          capture_output=True,text=True,timeout=15)
    assert result.returncode==0,(result.stdout,result.stderr)
    assert "refused: PASS" in result.stdout
    assert not list(workspace.glob("*MOVED*"))
    assert not list(workspace.glob("*HARDLINK*"))
print("Group manifest, inode, mode and restored-file snapshots: PASS")
