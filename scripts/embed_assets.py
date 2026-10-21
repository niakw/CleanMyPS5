#!/usr/bin/env python3
"""Embed the three web assets into portable C, with no runtime file loading."""
from pathlib import Path
import sys
import re
if len(sys.argv) != 5:
    raise SystemExit("usage: embed_assets.py output.h index.html styles.css app.js")
out, *paths = map(Path, sys.argv[1:])
version = (Path(__file__).resolve().parents[1] / "VERSION").read_text(encoding="utf-8").strip()
if not re.fullmatch(r"[0-9A-Za-z][0-9A-Za-z.\-]{0,30}", version):
    raise SystemExit("Invalid application VERSION")
parts = ["#include <stddef.h>\n", "static const char cmp5_version[] = \"" + version + "\";\n"]
for tag, path in zip(("index","styles","script"),paths):
    data = path.read_bytes()
    parts.append("static const unsigned char cmp5_"+tag+"[] = {\n")
    for start in range(0,len(data),16):
        parts.append("  "+", ".join("0x%02x"%b for b in data[start:start+16])+",\n")
    parts.append("};\nstatic const size_t cmp5_"+tag+"_len = "+str(len(data))+"u;\n")
out.parent.mkdir(parents=True,exist_ok=True)
out.write_text("".join(parts),encoding="utf-8")
