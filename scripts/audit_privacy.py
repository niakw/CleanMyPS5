#!/usr/bin/env python3
"""Scan tracked files for likely private data without printing captured values.

Use --history to inspect public commit author/committer email metadata separately.
The script cannot remove Git history or prove the absence of all secrets.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EMAIL = re.compile(rb"(?<![\w.+-])[\w.+-]+@(?:[A-Za-z0-9-]+\.)+[A-Za-z]{2,}\b")
HOME = re.compile(rb"(?<![A-Za-z0-9/_])/(?:Users|home)/[A-Za-z0-9._-]+(?=/|\b)")
LAN_IP = re.compile(rb"\b(?:192\.168|10\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}\b")
SECRET = re.compile(rb"(?:\bgh[pousr]_[A-Za-z0-9]{20,}\b|\bgithub_pat_[A-Za-z0-9_]{20,}\b|\bAKIA[0-9A-Z]{16}\b|\bsk-[A-Za-z0-9]{24,}\b|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----)")
EXAMPLE_DOMAINS = {b"example.com", b"example.net", b"example.org", b"users.noreply.github.com", b"localhost", b"invalid"}
BINARY_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".ico", ".zip", ".elf", ".bin", ".prx"}

def scan(content: bytes) -> list[tuple[str, int]]:
    if len(content) > 8 * 1024 * 1024 or b"\0" in content[:4096]:
        return []
    matches = []
    for label, pattern in (("credential", SECRET), ("email", EMAIL), ("home path", HOME), ("LAN address", LAN_IP)):
        for found in pattern.finditer(content):
            if label == "email" and found.group().rsplit(b"@", 1)[-1].lower() in EXAMPLE_DOMAINS:
                continue
            matches.append((label, content.count(b"\n", 0, found.start()) + 1))
    return matches

def history_count() -> int:
    output = subprocess.check_output(["git", "-C", str(ROOT), "log", "--all", "--format=%ae%n%ce"], text=True)
    emails = {line.strip().casefold() for line in output.splitlines()
              if "@" in line and not line.strip().casefold().endswith("@users.noreply.github.com")}
    print(f"Commit metadata: {len(emails)} distinct non-noreply address(es) in reachable history; values redacted")
    return len(emails)

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--history", action="store_true")
    parser.add_argument("--strict-history", action="store_true")
    args = parser.parse_args()
    names = subprocess.check_output(["git", "-C", str(ROOT), "ls-files", "-z"]).split(b"\0")
    issues = 0
    for raw in names:
        if not raw:
            continue
        name = raw.decode("utf-8", "replace")
        path = ROOT / name
        if path.suffix.lower() in BINARY_SUFFIXES or not path.is_file() or path.is_symlink():
            continue
        try:
            findings = scan(path.read_bytes())
        except OSError:
            continue
        for label, line in findings:
            print(f"{name}:{line}: potential private {label} (value redacted)")
            issues += 1
    history = history_count() if args.history or args.strict_history else 0
    if issues or (args.strict_history and history):
        print("Privacy audit: FAIL")
        return 1
    print("Privacy audit: PASS for current tracked text; history requires separate review")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
