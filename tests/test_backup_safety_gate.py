#!/usr/bin/env python3
"""Fail closed if an unvalidated write-capable backup engine is exposed on PS5."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
native=(root/"native/main.cpp").read_text(encoding="utf-8")
builder=(root/"scripts/build_native_app.sh").read_text(encoding="utf-8")
server=(root/"src/native/server.c").read_text(encoding="utf-8")
privilege=(root/"native/privilege_contract.hpp").read_text(encoding="utf-8")
assert '#include "privilege_contract.hpp"' in native
assert "cmp5_privilege::Operation::request_compression" in native
assert "default: return false;" in privilege
assert "privilegeEscalation" in server and "delete" in server
for token in ('#include "elevation.hpp"', 'elevation::request(',
              'seteuid(', 'setuid(', 'setgid(', 'setegid('):
    assert token not in native and token not in builder, \
        f"Unreviewed native title privilege operation: {token}"
sources=(root/"src/native/verified_backup.c").read_text(encoding="utf-8")

# Permit host-only test code, but never silently package writes into the title.
for token in ("verified_backup.h","verified_backup.c",
              "verified_group.h","verified_group.c",
              "cmp5_backup_media(","cmp5_restore_backup(",
              "cmp5_backup_group(","cmp5_restore_group(",
              "cmp5_inspect_group_recovery(",
              "cmp5_inspect_restored_group_recovery(",
              "cmp5_inspect_backup_restore_pair("):
    assert token not in native, f"Unvalidated native backup activation: {token}"
    assert token not in builder, f"Unvalidated title packaging: {token}"
    assert token not in server, f"Unvalidated helper endpoint: {token}"

# No gallery file deletion or renaming exists even in the isolated prototype.
assert "renameat(" not in sources
assert "rmdir(" not in sources
assert "unlinkat(dir,temp,0)" in sources
assert "unlinkat(target,temp,0)" in sources
assert "linkat(dir,temp,dir,id,0)" in sources
assert "snapshot_matches" in sources
# The extracted companion planner is allowed in the installed title only
# while it remains a purely read-only, bounded directory/metadata scanner.
companion=(root/"src/native/capture_companions.c").read_text(encoding="utf-8")
for forbidden in ("verified_backup.h", "verified_group.h", "O_WRONLY", "O_RDWR",
                  "O_CREAT", "O_TRUNC", "unlinkat(", "renameat(", "linkat(",
                  "fwrite(", "fopen(", "remove(", "rmdir("):
    assert forbidden not in companion, f"Unsafe companion planner: {forbidden}"
# The title is allowed to COUNT arbitrarily large video metadata, but the
# host backup planner/validator must retain the independent 512-MiB cap.
assert "1u,UINT64_MAX" in companion, "Read-only large capture inspection disabled"
assert "2u,MAX_TOTAL" in companion, "Host backup planner size cap relaxed"
assert "validate_capture_group_limit(parts,count,MAX_TOTAL)" in companion, \
    "Host group validator size cap relaxed"
assert "cmp5_plan_capture_group(" in companion
# Inconclusive scans may return an enum reason but must never expose
# candidate paths or raw gallery locators in the PS5 detail screen.
assert "Cmp5CompanionFailure" in companion
assert "CMP5_COMPANION_FAILURE_DIRECTORY_LIMIT" in companion
screen=native.split("void show_companions(",1)[1].split("void show_photo(",1)[0]
assert "preview_path" not in screen and "snapshot_inode" not in screen
assert "CMP5_COMPANION_FAILURE_DIRECTORY_LIMIT" in screen
assert "cmp5_capture_companions.c" in builder
assert "verified_group.c" not in builder
group=(root/"src/native/verified_group.c").read_text(encoding="utf-8")
assert "cmp5_backup_media(" in group and "cmp5_restore_backup(" in group
assert "cmp5_inspect_backup_restore_pair(" in group
assert "unlinkat" in group and "linkat" in group
assert "renameat(" not in group and "rmdir(" not in group
print("Backup engine safety gate: host-only, no gallery mutation, PASS")
