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
        "data/ExampleTool/cache/index.bin":b"CACHED",
        "data/ExampleTool/.cache/thumbnail.bin":b"HIDDEN!",
        "data/ExampleTool/tmp/session.part":b"WORK",
        "data/etaHEN/notice.txt":b"ETA",
        "user/app/TEST0001/eboot.bin":b"BINARY",
        "data/ExampleTool/saves/SECRET_SAVE.bin":b"PRIVATE",
    }
    for name,data in fixture.items():
        file=root/name
        file.parent.mkdir(parents=True,exist_ok=True)
        file.write_bytes(data)
    # A cache-named symlink must never be traversed as an app cache.
    (root/"data/ExampleTool/temp").symlink_to(root/"data/ExampleTool/saves",
                                              target_is_directory=True)
    missing_installed = root / "only-data"
    only_game = missing_installed / "data/homebrew/ExampleTool/eboot.elf"
    only_game.parent.mkdir(parents=True)
    only_game.write_bytes(b"ELF")
    empty_console = root / "empty-console"
    empty_console.mkdir()
    result=subprocess.run([sys.argv[1],str(root),str(missing_installed),str(empty_console)],
      check=False,text=True,capture_output=True,timeout=12)
    if result.returncode:
        raise AssertionError(f"Native homebrew fixture failed ({result.returncode}):\\n{result.stderr}\\n{result.stdout}")
    assert "Native homebrew summary, mandatory root coverage and cancellation: PASS" in result.stdout
    if len(sys.argv)>2:
        io_result=subprocess.run([sys.argv[2],str(root)],
          check=False,text=True,capture_output=True,timeout=12)
        if io_result.returncode:
            raise AssertionError(
                f"Homebrew fault-injection fixture failed ({io_result.returncode}):\n"
                f"{io_result.stderr}\n{io_result.stdout}")
        assert "Homebrew readdir and closedir failures" in io_result.stdout
    assert b"PRIVATE" == (root/"data/ExampleTool/saves/SECRET_SAVE.bin").read_bytes()
    assert b"CACHED" == (root/"data/ExampleTool/cache/index.bin").read_bytes()
    assert b"HIDDEN!" == (root/"data/ExampleTool/.cache/thumbnail.bin").read_bytes()
    assert b"WORK" == (root/"data/ExampleTool/tmp/session.part").read_bytes()
print("Homebrew native fixture: PASS")
