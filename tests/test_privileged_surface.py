#!/usr/bin/env python3
"""Synthetic attacks on native read-only compiler input gates."""
import importlib.util
from pathlib import Path

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location(
 "cmp5_security",root/"scripts/check_privileged_surface.py")
gate=importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(gate)
def reject(action):
 try: action()
 except ValueError: return
 raise AssertionError("Dangerous native operation incorrectly accepted")
gate.check(root)
helper_source=(root/"src/native/server.c").read_text()
assert "Diagnostic endpoint unavailable in PS5 helper" in helper_source
assert "if(is_head||" in helper_source
assert 'strcmp(path,"/api/overview")!=0' in helper_source
gate.check_reader("int inspect(void){return 0;}","safe")
for src in ("unlinkat(fd,name,0);","rename(a,b);",
            "open(path,O_CREAT|O_WRONLY);","seteuid(0);",
            "fwrite(data,1,size,out);","system(command);"):
 reject(lambda source=src:gate.check_reader(source,"attack"))
builder=(root/"scripts/build_native_app.sh").read_text()
assert gate.check_builder(builder)==gate.APPROVED | gate.BACKUP_UNITS
# The sole write-capable native exception is explicitly audited and may not
# become a generic system cleaner, even if an attacker modifies a header.
gate.check_backup_writer(
 (root/"native/private_media_backup.hpp").read_text(),
 "native/private_media_backup.hpp")
reject(lambda:gate.check_backup_writer(
 (root/"native/private_media_backup.hpp").read_text()+
 "\nint bad(){return seteuid(0);}\n",
 "native/private_media_backup.hpp"))
reject(lambda:gate.check_builder(
 builder.replace("CMP5_NATIVE_BACKUP_ONLY 1",
                 "CMP5_NATIVE_BACKUP_ONLY 0").replace(
 "sed -i '1i#define CMP5_NATIVE_BACKUP_ONLY 0'",
 "sed -i '1i#define CMP5_NATIVE_BACKUP_WRITABLE 1'")))
reject(lambda:gate.check_builder(
 builder+'\ncp "$project/src/native/verified_group.c" "$tmp/native-toolchain/src/cmp5_verified_group.c"\n'
))
reject(lambda:gate.check_builder(builder.replace(
 'cp "$project/src/native/inventory.c" "$tmp/native-toolchain/src/cmp5_inventory.c"',
 'cp "$project/src/native/verified_group.c" "$tmp/native-toolchain/src/cmp5_verified_group.c"'
)))
for src in (
 "linkat(fd,a,fd,b,0);","symlinkat(a,fd,b);",
 "fchmodat(fd,name,0777,0);","fchownat(fd,name,0,0,0);",
 "execve(path,args,env);","posix_spawn(&pid,path,0,0,args,env);",
 "syscall(5,a,b);","kernel_copyin(buffer,kaddr,len);",
 "sceKernelUnlink(path);","openat(fd,name,O_TMPFILE);",
):
 reject(lambda value=src:gate.check_reader(value,"injected-mutation"))
gate.check_title_headers(root,builder,gate.APPROVED)
reject(lambda:gate.check_title_headers(
 root,builder.replace(
 'cp "$project/src/native/game_scan_identity.h" "$tmp/native-toolchain/src/game_scan_identity.h"',
 '# intentionally omitted'),gate.APPROVED))
# Reject a write hidden in a newly staged inline header, not just C sources.
import tempfile
with tempfile.TemporaryDirectory() as tmp:
 sandbox=Path(tmp)
 (sandbox/"src/native").mkdir(parents=True)
 (sandbox/"native").mkdir()
 (sandbox/"src/native"/"fixture.c").write_text('#include "fixture.h"\n')
 (sandbox/"src/native"/"fixture.h").write_text(
  'int bad(void){return unlinkat(3,"a",0);}')
 (sandbox/"native"/"main.cpp").write_text('#include "fixture.h"\n')
 (sandbox/"native"/"preview_decode.c").write_text("")
 staged=builder+'\ncp "$project/src/native/fixture.h" "$tmp/native-toolchain/src/fixture.h"'
 reject(lambda:gate.check_title_headers(sandbox,staged,{"fixture.c"}))
reject(lambda:gate.check_builder(builder+
 '\ncp "$project/native/elevation.cpp" "$tmp/native-toolchain/src/elevation.cpp"'))
reject(lambda:gate.check_builder(builder+
 '\ncp "$project/src/native/secret_writer.c" "$tmp/native-toolchain/src/cmp5_secret_writer.c"'))
reject(lambda:gate.check_builder(builder.replace(
 'cp "$project/native/main.cpp" "$tmp/native-toolchain/src/main.cpp"',
 'cp "$project/native/main.cpp" "$tmp/native-toolchain/src/unsafe.cpp"')))
# The diagnostic payload is built from Makefile, NOT the native app overlay.
# Ensure a new source added to the payload cannot bypass read-only review.
makefile=(root/"Makefile").read_text()
assert gate.parse_helper_sources(makefile)=={
 "src/native/"+name for name in gate.HELPER_UNITS}
reject(lambda:gate.parse_helper_sources(
 makefile.replace("-DPS5 -DCMP5_DIAGNOSTIC_HELPER -Isrc/native",
                  "-DPS5 -Isrc/native")))
gate.check_helper_closure(root)
reject(lambda:gate.parse_helper_sources(makefile.replace(
 "src/native/homebrew.c\nall: server",
 "src/native/homebrew.c src/native/unreviewed_writer.c\nall: server")))
reject(lambda:gate.parse_helper_sources(makefile.replace(
 "$(NATIVE) -lSceSystemService -lSceUserService -o cleanmyps5-helper.elf",
 "src/native/unsafe.c -lSceSystemService -lSceUserService -o cleanmyps5-helper.elf")))
# The helper has separate first-party includes, also including media,
# video, volume and preflight units absent from the installed title.
print("Privilege scanner and injected write operations: PASS")
