#!/usr/bin/env python3
"""Fail CI when a tracked file appears to contain private credentials or host paths.

Only the tracked working tree is checked here. Git history and off-repository
console files require independent review. Potential matches are never printed.
"""
from __future__ import annotations

import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
tracked = subprocess.check_output(["git", "-C", str(ROOT), "ls-files", "-z"]).split(b"\0")
restricted_suffixes = {".p12", ".pfx", ".kdbx", ".mobileprovision"}
restricted_names = {".env", "id_rsa", "id_ed25519", ".netrc", ".npmrc", "credentials.json"}
patterns = {
    "GitHub credential": re.compile(r"(?:gh[pousr]_[A-Za-z0-9_]{30,}|github_pat_[A-Za-z0-9_]{45,})"),
    "AWS access key": re.compile(r"AKIA[A-Z0-9]{16}"),
    "private key header": re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    "credentials in URL": re.compile(r"(?:https?|ftps?)://[^/@:\s]+:[^/@\s]+@"),
    "developer home directory": re.compile(r"(?:/Users|/home)/[A-Za-z0-9_.-]{3,}/"),
    "Stripe credential": re.compile(r"(?:sk_live_|rk_live_)[A-Za-z0-9]{18,}"),
}
findings: list[str] = []
scanned = 0
for raw in tracked:
    if not raw:
        continue
    rel = Path(raw.decode("utf-8", errors="replace"))
    if rel.name.casefold() in restricted_names or rel.suffix.casefold() in restricted_suffixes:
        findings.append(f"{rel}: prohibited tracked file type")
    try:
        data = (ROOT / rel).read_bytes()
    except OSError:
        continue
    if b"\0" in data[:4096]:
        continue
    scanned += 1
    contents = data.decode("utf-8", errors="replace")
    for name, pattern in patterns.items():
        # Line numbers identify source locations without disclosing matched values.
        for hit in pattern.finditer(contents):
            line = contents.count("\n", 0, hit.start()) + 1
            findings.append(f"{rel}:{line}: {name}")
if findings:
    raise SystemExit("Potential private material found:\n" + "\n".join(findings))
print(f"Repository hygiene: PASS ({scanned} tracked text files inspected; history not scanned)")
