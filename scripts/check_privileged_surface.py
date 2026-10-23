#!/usr/bin/env python3
"""Validate the exact native read-only compiler surface (defense in depth)."""
from pathlib import Path
import re

APPROVED = frozenset({
 "inventory.c", "duplicates.c", "homebrew.c", "game_images.c",
 "capture_companions.c", "diagnostics.c", "media_preview.c",
 "video_metadata.c", "webm_metadata.c"
})
BAD = re.compile(
 r"\b(?:unlink|unlinkat|remove|rename|renameat|renameat2|rmdir|mkdir|"
 r"mkdirat|chown|fchown|chmod|fchmod|truncate|ftruncate|mkstemp|"
 r"mkdtemp|fopen|freopen|fwrite|setuid|seteuid|setgid|setegid|"
 r"setreuid|setresuid|ptrace|system|popen)\s*\("
 r"|\bO_(?:CREAT|TRUNC|WRONLY|RDWR|APPEND)\b"
)

def check_reader(content: str, name: str) -> None:
 match = BAD.search(content)
 if match:
  raise ValueError(f"{name}: unreviewed filesystem write or credential mutation: {match.group()}")

def check_builder(content: str) -> set[str]:
 sources = re.findall(
  r'cp "\$project/src/native/([A-Za-z0-9_]+\.c)" '
  r'"\$tmp/native-toolchain/src/[A-Za-z0-9_]+\.c"', content)
 if len(sources)!=len(APPROVED) or set(sources)!=APPROVED:
  raise ValueError("Native compiler source list differs from reviewed readers")
 if 'cp "$project/native/preview_decode.c"' not in content:
  raise ValueError("Native preview decoder source missing")
 if "verified_group.c" in content or "verified_backup.c" in content:
  raise ValueError("Unvalidated host-only backup/restore linked into PS5")
 return set(sources)

def check(root: Path) -> None:
 builder=(root/"scripts/build_native_app.sh").read_text()
 title=(root/"native/main.cpp").read_text()
 helper=(root/"src/native/server.c").read_text()
 if '#include "privilege_contract.hpp"' not in title:
  raise ValueError("Missing role contract in native title")
 for name in check_builder(builder):
  check_reader((root/"src/native"/name).read_text(),name)
 check_reader(title,"native/main.cpp")
 check_reader((root/"native/preview_decode.c").read_text(),"preview_decode.c")
 check_reader(helper,"server.c")
 if 'strcmp(method,"GET")!=0&&!is_head' not in helper or (
      'if(is_head && strncmp(path,"/api/video?file=",16)!=0)' not in helper):
  raise ValueError("Optional diagnostic helper no longer limits GET/HEAD")
 if "privilegeEscalation" not in helper or "delete" not in helper:
  raise ValueError("Helper capability identity no longer read-only")
 if '#include "elevation.hpp"' in title or "elevation::request(" in title:
  raise ValueError("Unreviewed native process elevation")

if __name__=="__main__":
 check(Path(__file__).resolve().parents[1])
 print("Native PS5 actor and write surface: PASS")
