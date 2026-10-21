#!/usr/bin/env python3
"""Bounded audit of reachable Git *blob history* without disclosing values.

Informational by default; history rewrites must be separately reviewed.
Only inspected objects are counted: unreachable, deleted or external refs
are outside scope. Requires an unshallowed checkout.
"""
from __future__ import annotations
from collections import Counter
import subprocess
from pathlib import Path
from audit_privacy import scan, BINARY_SUFFIXES

ROOT = Path(__file__).resolve().parents[1]
MAX_BLOB = 8 * 1024 * 1024

def git(*args: str) -> bytes:
    return subprocess.check_output(["git", "-C", str(ROOT), *args])

def main() -> int:
    if git("rev-parse", "--is-shallow-repository").strip() != b"false":
        raise SystemExit("Full Git history not present; fetch-depth: 0 required")
    objects = {}
    for record in git("rev-list", "--objects", "--all").splitlines():
        sha, _, path = record.partition(b" ")
        objects.setdefault(sha.decode("ascii"), path.decode("utf-8", "replace"))
    totals = Counter()
    examined = 0
    capped = 0
    findings = []
    for sha, path in objects.items():
        if not path or Path(path).suffix.lower() in BINARY_SUFFIXES:
            continue
        if git("cat-file", "-t", sha).strip() != b"blob":
            continue
        if int(git("cat-file", "-s", sha)) > MAX_BLOB:
            capped += 1
            continue
        content = git("cat-file", "blob", sha)
        examined += 1
        for label, line in scan(content):
            totals[label] += 1
            # Never print matching strings; only source locations and short SHA.
            if len(findings) < 30:
                findings.append((path, line, label, sha[:10]))
    print(f"Historical blob audit: {examined} text objects checked; {capped} oversized objects skipped")
    for label, count in sorted(totals.items()):
        print(f"Potential historical {label}: {count} match(es)")
    for path, line, kind, sha in findings:
        print(f"  {path}:{line} (blob {sha}): potential {kind}; value redacted")
    if findings:
        print("Review these historical objects before any history rewrite or public release.")
    elif capped:
        print("No matches within scanned size bounds; oversized files were not inspected.")
    else:
        print("No matching private patterns in the reachable text blob history.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
