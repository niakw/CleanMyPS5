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

HELPER_UNITS = frozenset({
 "server.c", "inventory.c", "diagnostics.c", "duplicates.c",
 "media.c", "volumes.c", "video.c", "preflight.c", "homebrew.c",
})

def parse_helper_sources(makefile: str) -> set[str]:
 """Allow only the intended read-only sources for the native helper target."""
 matches = re.findall(r"^NATIVE\s*=\s*(.+)$",makefile,re.MULTILINE)
 if len(matches)!=1:
  raise ValueError("Missing or duplicate helper source list")
 tokens = matches[0].split()
 expected = {"src/native/"+unit for unit in HELPER_UNITS}
 if len(tokens)!=len(expected) or set(tokens)!=expected:
  raise ValueError("Unexpected native diagnostic helper compiler inputs")
 # Match the real payload make rule: a private list does not protect a
 # separate unrestricted compile command.
 if not re.search(r"^ps5-helper:.*\n(?:\t[^\n]*\n)*"
                  r"\t[^\n]*\$\(PS5_CC\)[^\n]*\$\(NATIVE\)",
                  makefile,re.MULTILINE):
  raise ValueError("PS5 helper no longer compiles approved NATIVE sources")
 helper_compile = re.search(
  r"(?m)^\t[^\n]*\$\(PS5_CC\)[^\n]*-o cleanmyps5-helper\.elf$",
  makefile)
 if not helper_compile or "-DCMP5_DIAGNOSTIC_HELPER" not in helper_compile.group():
  raise ValueError("PS5 helper must use its isolated diagnostic compile mode")
 return set(tokens)

def check_helper_closure(root: Path) -> None:
 """Audit the exact C and locally included headers of the helper payload."""
 units=parse_helper_sources((root/"Makefile").read_text())
 todo=[root/name for name in sorted(units)]
 checked:set[Path]=set()
 while todo:
  path=todo.pop()
  if path in checked:
   continue
  if not path.is_file():
   raise ValueError(f"Missing helper source: {path}")
  checked.add(path)
  content=path.read_text()
  check_reader(content,str(path.relative_to(root)))
  for name in re.findall(r'^\s*#\s*include\s+"([^"]+)"',
                         content,flags=re.MULTILINE):
   if name in ("scan_rules.h","assets.h"):
    continue
   if not re.fullmatch(r"[A-Za-z0-9_]+\.h",name):
    raise ValueError(f"Unexpected helper include: {name}")
   header=root/"src/native"/name
   if not header.is_file():
    raise ValueError(f"Unknown helper header: {name}")
   todo.append(header)

def check_builder(content: str) -> set[str]:
 sources = re.findall(
  r'cp "\$project/src/native/([A-Za-z0-9_]+\.c)" '
  r'"\$tmp/native-toolchain/src/[A-Za-z0-9_]+\.c"', content)
 if len(sources)!=len(APPROVED) or set(sources)!=APPROVED:
  raise ValueError("Native compiler source list differs from reviewed readers")
 if 'cp "$project/native/preview_decode.c"' not in content:
  raise ValueError("Native preview decoder source missing")
 # Any extra or renamed C/C++ overlay changes what executes inside
 # the app. Treat it as a security boundary, not a loose file count.
 overlays = re.findall(
  r'cp "\$project/(src/native|native)/([A-Za-z0-9_]+\.(?:c|cc|cpp|cxx))" '
  r'"\$tmp/native-toolchain/src/([A-Za-z0-9_]+\.(?:c|cc|cpp|cxx))"',content)
 expected = {("src/native",name,"cmp5_"+name) for name in APPROVED}
 expected.update({("native","main.cpp","main.cpp"),
                  ("native","preview_decode.c","cmp5_preview_decode.c")})
 if len(overlays)!=len(expected) or set(overlays)!=expected:
  raise ValueError("Unreviewed or renamed C/C++ native title overlay")
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
 check_helper_closure(root)
 if 'strcmp(method,"GET")!=0&&!is_head' not in helper or (
      'if(is_head && strncmp(path,"/api/video?file=",16)!=0)' not in helper):
  raise ValueError("Optional diagnostic helper no longer limits GET/HEAD")
 if "privilegeEscalation" not in helper or "delete" not in helper:
  raise ValueError("Helper capability identity no longer read-only")
 if ('#if defined(PS5) && defined(CMP5_DIAGNOSTIC_HELPER)\n'
     ' /* Do not turn a possibly more privileged helper') not in helper:
  raise ValueError("Missing helper-only diagnostic route restriction")
 if '#error Diagnostic helper cannot start a browser' not in helper:
  raise ValueError("Helper/browser mode separation lost")
 routes = (
  'strcmp(path,"/api/status")!=0',
  'strcmp(path,"/api/capabilities")!=0',
  'strcmp(path,"/api/preflight")!=0',
  'strcmp(path,"/api/overview")!=0',
 )
 for route in routes:
  if route not in helper:
   raise ValueError("PS5 diagnostic route allowlist changed")
 if 'plain(fd,404,"Diagnostic endpoint unavailable in PS5 helper")' not in helper:
  raise ValueError("PS5 helper denies no unsupported routes")
 if '#include "elevation.hpp"' in title or "elevation::request(" in title:
  raise ValueError("Unreviewed native process elevation")

if __name__=="__main__":
 check(Path(__file__).resolve().parents[1])
 print("Native PS5 actor and write surface: PASS")
