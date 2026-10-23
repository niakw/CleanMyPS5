#!/usr/bin/env python3
"""Fail the PS5 title build if the modified bundled engine loses safety gates.

This checks the *actual transformed C*, not the patcher's intention.
It is a defense-in-depth structural check; C regression tests and hardware
testing remain necessary.
"""
from pathlib import Path
import sys


def check_engine(root: Path) -> None:
    http = (root / "src/httpd.c").read_text()
    main = (root / "src/main.c").read_text()
    required = {
        "loopback-only listener": "addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);",
        "no automated pruning": "r.clean_leftovers = false;",
        "source preservation": "r.remove_source = false;",
        "no replacement": "r.overwrite = false;",
        "no force mode": "r.force = false;",
        "mandatory integrity check": "r.verify = true;",
        "dedup": "r.dedup = true;",
        "no spillover cache": "r.cache_mode = 0;",
        "image metadata endpoint": '"/api/cmp5-image"',
        "require SMP for direct images": "image_title_required",
        "SMP image ownership": "strcmp(owner.path, r.source) != 0",
        "SMP managed source": "!owner.managed || !owner.source_available",
        "SMP identity": "strcmp(owner.title_id, r.smp_title) != 0",
        "image owner source type": 'strcmp(owner.source_type, "image") != 0',
        "source no hardlinks": "source_st.st_nlink != 1",
        "source size field": 'form_get(body, "cmp5_size"',
        "source size recheck": "(uint64_t)source_st.st_size != expected",
        "post-preflight source identity check": "verified_st.st_ino != source_st.st_ino",
        "post-preflight source exact mode check": "verified_st.st_mode != source_st.st_mode",
        "post-preflight source nanosecond check": "verified_st.st_mtim.tv_nsec != source_st.st_mtim.tv_nsec",
        "post-preflight source mutation denial": "image_mutated_during_preflight",
        "engine live image preflight": "job_preflight(r.source, r.smp_title, r.dest_base",
        "preflight errors must be empty": 'strstr(preflight_json, "\\"errors\\":[]")',
        "post source no deletion": '"/api/delete-source"',
        "no cancellation deleting partial pack": '"/api/cancel"',
        "protected profile writes": '"/api/profile"',
        "compact read-only progress": '"/api/cmp5-progress"',
    }
    missing = [label for label, fragment in required.items() if fragment not in http]
    if "addr.sin_addr.s_addr = htonl(INADDR_ANY);" in http:
        missing.append("INADDR_ANY was retained")
    for label, fragment in {
        "unexpected startup cleanup": "  clean_stray_mount_dirs();",
        "unexpected plugin downloads": "  plugins_start_background();",
        "unexpected cover downloads": "  covers_start();",
        "unexpected USB profile imports": "  e2a_profile_import_usb();",
        "unexpected game directory creation": "  e2a_mkdir_p(E2A_HOMEBREW_DIR);",
        "unexpected automatic jobs": "  if (auto_mode) auto_queue();",
    }.items():
        for line in main.splitlines():
            if line.strip() == fragment.strip():
                missing.append(label)
                break
    if missing:
        raise SystemExit("Embedded engine safety policy FAILED: " + ", ".join(missing))
    print("Bundled AMPR safety policy after source transform: PASS")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_embedded_engine_policy.py MODIFIED_ENGINE_ROOT")
    check_engine(Path(sys.argv[1]))
