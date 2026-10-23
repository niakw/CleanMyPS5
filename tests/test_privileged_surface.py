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
gate.check_reader("int inspect(void){return 0;}","safe")
for src in ("unlinkat(fd,name,0);","rename(a,b);",
            "open(path,O_CREAT|O_WRONLY);","seteuid(0);",
            "fwrite(data,1,size,out);","system(command);"):
 reject(lambda source=src:gate.check_reader(source,"attack"))
builder=(root/"scripts/build_native_app.sh").read_text()
assert gate.check_builder(builder)==gate.APPROVED
reject(lambda:gate.check_builder(
 builder+'\ncp "$project/src/native/verified_group.c" "$tmp/native-toolchain/src/cmp5_verified_group.c"\n'
))
reject(lambda:gate.check_builder(builder.replace(
 'cp "$project/src/native/inventory.c" "$tmp/native-toolchain/src/cmp5_inventory.c"',
 'cp "$project/src/native/verified_group.c" "$tmp/native-toolchain/src/cmp5_verified_group.c"'
)))
print("Privilege scanner and injected write operations: PASS")
