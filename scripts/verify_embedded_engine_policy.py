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
        "strict source root whitelist": "cmp5_game_source_allowed(r.source)",
        "strict destination whitelist": "cmp5_game_destination_allowed(r.dest_base)",
        "source descriptor-anchored admission": 'cmp5_game_path_stat("/", r.source, false, &cmp5_source_snapshot)',
        "destination descriptor-anchored admission": 'cmp5_game_path_stat("/", r.dest_base, true, &cmp5_dest_snapshot)',
        "reject aliases and disappeared games": "game_path_unstable_or_aliased",
        "source inode and timestamp recheck": 'cmp5_game_path_still_same("/", r.source, false,',
        "destination inode and timestamp recheck": 'cmp5_game_path_still_same("/", r.dest_base, true,',
        "reject source/destination replacement": "game_identity_changed_during_preflight",
        "managed image output must be free": "game_output_already_present",
        "managed image output must remain free": "game_output_occupied_before_enqueue",
        "no arbitrary source access": "game_path_not_allowed",
        "write endpoints default deny": "write_operation_not_allowed",
        "only explicit pause path": 'strcmp(path, "/api/pause")',
        "only explicit resume path": 'strcmp(path, "/api/resume")',
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
    # One admission at request parsing and another after the potentially
    # long ShadowMountPlus preflight, immediately before enqueuing work.
    exact_checks = (
        'cmp5_game_path_stat("/", r.source, false, &cmp5_source_snapshot)',
        'cmp5_game_path_stat("/", r.dest_base, true, &cmp5_dest_snapshot)',
        'cmp5_game_path_still_same("/", r.source, false,',
        'cmp5_game_path_still_same("/", r.dest_base, true,',
    )
    if any(http.count(fragment) != 1 for fragment in exact_checks):
        missing.append("missing initial/second path identity checks")
    if http.count('cmp5_game_output_vacant("/", r.dest_base, r.smp_title)') != 2:
        missing.append("managed image output missing either collision guard")
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
