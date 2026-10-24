#!/usr/bin/env python3
"""Host-only protected homebrew fixture, entirely read-only."""
from pathlib import Path
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory)
    fixture={
        "data/homebrew/ExampleTool/eboot.elf":b"ELF",
        "data/etaHEN/plugins/plugin.elf":b"PLUGIN",
        "data/ExampleTool/config.json":b"{}",
        "data/ExampleTool/public.bin":b"USER",
        "data/etaHEN/notice.txt":b"ETA",
        "user/app/TEST0001/eboot.bin":b"BINARY",
        "data/ExampleTool/saves/SECRET_SAVE.bin":b"PRIVATE",
    }
    for name,data in fixture.items():
        file=root/name
        file.parent.mkdir(parents=True,exist_ok=True)
        file.write_bytes(data)
    missing_installed = root / "only-data"
    only_game = missing_installed / "data/homebrew/ExampleTool/eboot.elf"
    only_game.parent.mkdir(parents=True)
    only_game.write_bytes(b"ELF")
    empty_console = root / "empty-console"
    empty_console.mkdir()
    result=subprocess.run([sys.argv[1],str(root),str(missing_installed),str(empty_console)],
      check=True,text=True,capture_output=True,timeout=12)
    assert "Native homebrew summary, mandatory root coverage and cancellation: PASS" in result.stdout
    if len(sys.argv)>2:
        io_result=subprocess.run([sys.argv[2],str(root)],
          check=True,text=True,capture_output=True,timeout=12)
        assert "Homebrew readdir and closedir failures" in io_result.stdout
    assert b"PRIVATE" == (root/"data/ExampleTool/saves/SECRET_SAVE.bin").read_bytes()
print("Homebrew native fixture: PASS")
