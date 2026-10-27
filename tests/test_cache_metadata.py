#!/usr/bin/env python3
"""Read-only native scanner against synthetic caches mutated by the TEST."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import time

with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    (root / "data/homebrew").mkdir(parents=True)
    (root / "data/etaHEN/plugins").mkdir(parents=True)
    (root / "user/app").mkdir(parents=True)
    cache = root / "data/ExampleApp/cache"
    cache.mkdir(parents=True)
    path = cache / "same.bin"
    path.write_bytes(b"ABCDEF")
    old_ns = time.time_ns() - 3 * 86400 * 10**9
    os.utime(path, ns=(old_ns, old_ns))
    os.utime(cache, ns=(old_ns, old_ns))

    def capture():
        result = subprocess.run(
            [sys.argv[1], str(root)], capture_output=True,
            text=True, timeout=12, check=True)
        return tuple(int(v) for v in result.stdout.strip().split())

    baseline = capture()
    assert baseline == capture(), "a stable tree changed its metadata signature"
    assert baseline[2] >= 2

    # Replace bytes with a SAME-SIZE body and a controlled, different
    # timestamp. The production scanner never opens that body for hashing.
    path.write_bytes(b"UVWXYZ")
    new_ns = old_ns + 20 * 10**9
    os.utime(path, ns=(new_ns, new_ns))
    rewritten = capture()
    assert rewritten != baseline, "same-size file rewrite was invisible"
    assert rewritten == capture(), "rewritten metadata was not repeatable"

    renamed = cache / "other.bin"
    path.rename(renamed)
    moved = capture()
    assert moved != rewritten, "same-size inode rename was invisible"
    assert moved == capture()
    assert renamed.read_bytes() == b"UVWXYZ"

# A separate synthetic workspace exercises the production 96-entry bound,
# including a late-discovered large cache and explicit partial coverage.
with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    (root / "data/homebrew").mkdir(parents=True)
    (root / "data/etaHEN/plugins").mkdir(parents=True)
    (root / "user/app").mkdir(parents=True)
    for i in range(100):
        folder = root / f"data/APP{i:03d}/cache"
        folder.mkdir(parents=True)
        (folder / "item.bin").write_bytes(b"Z" * (i + 1))
    check = subprocess.run(
        [sys.argv[1], str(root), "overflow"],
        check=True, capture_output=True, text=True, timeout=20)
    assert "overflow fail-closed: PASS" in check.stdout

print("Native metadata rewrites, renames and 96-cache overflow: PASS")
