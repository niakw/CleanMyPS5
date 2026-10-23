#!/usr/bin/env python3
"""Validate the exact native read-only compiler surface (defense in depth)."""
from pathlib import Path
import re

APPROVED = frozenset({
 "inventory.c", "duplicates.c", "homebrew.c", "game_images.c",
 "capture_companions.c", "diagnostics.c", "media_preview.c",
 "video_metadata.c", "webm_metadata.c"
})
# Deny mutation paths in all reviewed read-only first-party code.
BAD = re.compile(
 r"\b(?:unlink|unlinkat|remove|rename|renameat|renameat2|rmdir|mkdir|"
 r"mkdirat|chown|fchown|fchownat|lchown|chmod|fchmod|fchmodat|"
 r"truncate|ftruncate|mkstemp|mkdtemp|creat|link|linkat|symlink|"
 r"symlinkat|mknod|mknodat|fopen|freopen|fwrite|fdopen|"
 r"setuid|seteuid|setgid|setegid|setreuid|setresuid|setgroups|"
 r"setregid|setresgid|ptrace|system|popen|execve|execv|execl|"
 r"execvp|posix_spawn|posix_spawnp|syscall|dlopen|dlsym|"
 r"kernel_copyin|kernel_write|sceKernelWrite|sceKernelUnlink|"
 r"sceKernelRename)\s*\("
 r"|\bO_(?:CREAT|TRUNC|WRONLY|RDWR|APPEND|TMPFILE)\b"
)

def check_reader(content: str, name: str) -> None:
 match = BAD.search(content)
 if match:
  raise ValueError(f"{name}: unreviewed filesystem write or credential mutation: {match.group()}")

GENERATED_HEADERS = frozenset({"demo_renderer.hpp", "scan_rules.h", "stb_image.h"})

def check_title_headers(root: Path, builder: str, sources: set[str]) -> None:
 """Verify the native app's transitive first-party header closure and stage list."""
 staging = re.findall(
  r'cp "\$project/(src/native|native)/([A-Za-z0-9_]+\.(?:h|hpp))" '
  r'"\$tmp/native-toolchain/src/([A-Za-z0-9_]+\.(?:h|hpp))"', builder)
 copied = {(area, name) for area, name, dest in staging if name == dest}
 if len(staging) != len(copied):
  raise ValueError("Duplicate or remapped native title header staging")
 todo = [root/"native/main.cpp", root/"native/preview_decode.c"]
 todo += [root/"src/native"/name for name in sorted(sources)]
 checked: set[Path] = set()
 while todo:
  path = todo.pop()
  if path in checked:
   continue
  if not path.is_file():
   raise ValueError(f"Missing native source: {path}")
  checked.add(path)
  text = path.read_text()
  check_reader(text, str(path.relative_to(root)))
  includes = re.findall(r'^\s*#\s*include\s+"([^"]+)"',
                        text, flags=re.MULTILINE)
  for name in includes:
   if name in GENERATED_HEADERS:
    continue
   if not re.fullmatch(r"[A-Za-z0-9_]+\.(?:h|hpp)", name):
    raise ValueError(f"Unsafe native include: {name}")
   if (root/"native"/name).is_file():
    area = "native"
   elif (root/"src/native"/name).is_file():
    area = "src/native"
   else:
    raise ValueError(f"Unreviewed native header: {name}")
   if (area,name) not in copied:
    raise ValueError(f"Header missing from PS5 title overlay: {name}")
   todo.append(root/area/name)

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
 compiled=check_builder(builder)
 check_title_headers(root,builder,compiled)
 check_reader(helper,"server.c")
 for name in re.findall(r'^\s*#\s*include\s+"([^"]+)"',
                        helper,flags=re.MULTILINE):
  if name=="assets.h":
   continue
  if not re.fullmatch(r"[A-Za-z0-9_]+\.h",name):
   raise ValueError(f"Unsafe helper header: {name}")
  header=root/"src/native"/name
  if not header.is_file():
   raise ValueError(f"Unreviewed helper header: {name}")
  check_reader(header.read_text(),str(header.relative_to(root)))
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
