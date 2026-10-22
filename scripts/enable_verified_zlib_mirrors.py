#!/usr/bin/env python3
"""Fail-closed mirror fallback for pinned zlib native dependency bootstrap.

The upstream template is modified only within the temporary build checkout.
Both URLs are official zlib.net domains, both must match the original SHA-256.
"""
from pathlib import Path
import sys

ORIGINAL = '''    if [[ ! -f $zlib_archive ]]; then
        wget -q "$zlib_url" -O "$zlib_archive.download"
        mv "$zlib_archive.download" "$zlib_archive"
    fi
    printf '%s  %s\\n' "$zlib_hash" "$zlib_archive" | sha256sum --check --strict >/dev/null'''

REPLACEMENT = '''    if [[ ! -f $zlib_archive ]]; then
        # Official mirrors only, pinned SHA-256 required *before* publication.
        verified=false
        for url in "$zlib_url" "https://www.zlib.net/fossils/zlib-$zlib_version.tar.gz"; do
            if wget -q --tries=2 --timeout=25 "$url" -O "$zlib_archive.download" &&
               printf '%s  %s\\n' "$zlib_hash" "$zlib_archive.download" |
                    sha256sum --check --strict >/dev/null 2>&1; then
                mv "$zlib_archive.download" "$zlib_archive"
                verified=true
                break
            fi
            rm -f -- "$zlib_archive.download"
        done
        if [[ $verified != true ]]; then
            echo "Pinned zlib SHA-256 unavailable from both official mirrors" >&2
            exit 1
        fi
    fi
    printf '%s  %s\\n' "$zlib_hash" "$zlib_archive" | sha256sum --check --strict >/dev/null'''

def patch(path: Path) -> None:
    source=path.read_text(encoding="utf-8")
    if source.count(ORIGINAL)!=1 or source.count('zlib_version="1.3.2"')!=1 or (
        source.count('zlib_hash="bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16"')!=1
    ):
        raise ValueError("Unexpected native zlib pinning script; refusing mirror patch")
    path.write_text(source.replace(ORIGINAL,REPLACEMENT),encoding="utf-8")

if __name__=="__main__":
    if len(sys.argv)!=2:raise SystemExit("usage: enable_verified_zlib_mirrors.py SETUP_SCRIPT")
    try:patch(Path(sys.argv[1]))
    except (OSError,ValueError) as exc:raise SystemExit(f"zlib mirror safety error: {exc}") from exc
