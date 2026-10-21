#!/usr/bin/env python3
"""Reject third-party product branding in the CURRENT tracked tree.

Patterns are assembled from neutral fragments to avoid reintroducing complete
third-party names in this very guard. Historical Git commits are not rewritten.
"""
from __future__ import annotations

import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
names = subprocess.check_output(
    ["git", "-C", str(ROOT), "ls-files", "-z"]
).split(b"\0")
forbidden = (
    "clean" + "my" + "mac",
    "mac" + "paw",
    "cc" + "leaner",
    "my" + " clutter",
    "space" + " lens",
    "smart" + " care",
    "my" + " tools",
)
violations: list[str] = []
for item in names:
    if not item:
        continue
    path = Path(item.decode("utf-8", errors="replace"))
    label = str(path).casefold()
    if any(bad in label for bad in forbidden):
        violations.append(f"filename: {path}")
    full = ROOT / path
    try:
        contents = full.read_text(encoding="utf-8").casefold()
    except (UnicodeError, OSError):
        continue
    if any(bad in contents for bad in forbidden):
        violations.append(f"content: {path}")
if violations:
    raise SystemExit("Non-neutral branding found:\n" + "\n".join(violations))
print("Neutral repository branding: PASS")
