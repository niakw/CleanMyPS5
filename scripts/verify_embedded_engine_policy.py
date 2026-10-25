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
    job = (root / "src/job.c").read_text()
    game_paths = (root / "src/cmp5_compression_allowed_paths.h").read_text()
    required = {
        "progress phase whitelist": 'const char *phase="other",*reason="none";',
        "progress reason only for paused": "if(status.state==JOB_PAUSED)",
        "progress enum code bounded": 'strncmp(status.code,reasons[i],sizeof(status.code))',
        "scoped pause endpoint": 'cmp5_job_pause_dest(pause_dest)',
        "scoped pause rejects wrong game": "pause_target_changed",
        "loopback-only listener": "addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);",
        "no automated pruning": "r.clean_leftovers = false;",
        "source preservation": "r.remove_source = false;",
        "no replacement": "r.overwrite = false;",
        "no caller-selected output name": "r.name[0] = 0;",
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
        "image lookup source allowlist": "cmp5_game_source_allowed(path)",
        "image lookup anchored source": 'cmp5_game_path_stat("/",path,false,&st)',
        "image lookup anchored recheck": 'cmp5_game_path_stat("/",path,false,&after)',
        "image lookup snapshot identity": 'cmp5_game_same_stat(&st,&after)',
        "check preflight source allowlist": "cmp5_game_source_allowed(p)",
        "check preflight destination allowlist": "cmp5_game_destination_allowed(dst)",
        "check preflight anchored source": 'cmp5_game_path_stat("/",p,false,&cmp5_check_source)',
        "check preflight anchored destination": 'cmp5_game_path_stat("/",dst,true,&cmp5_check_dest)',
        "check preflight denies forbidden trees": "game_preflight_path_not_allowed",
        "check preflight verifies SMP owner": "smp_game_info(smp,&cmp5_owner)",
        "check preflight exact SMP path": '!cmp5_game_smp_field_eq(cmp5_owner.path,sizeof(cmp5_owner.path),p)',
        "check preflight rejects mismatched owner": "game_preflight_owner_mismatch",
        "check preflight source revalidation": 'cmp5_game_path_still_same("/",p,false,&cmp5_check_source)',
        "check preflight destination revalidation": 'cmp5_game_path_still_same("/",dst,true,&cmp5_check_dest)',
        "check preflight rejects stale metadata": "game_preflight_source_changed",
        "bounded SMP title": "strnlen(g->title_id,sizeof(g->title_id))",
        "bounded SMP path": "cmp5_game_smp_field_eq(items[i].path,sizeof(items[i].path),path)",
        "bounded SMP source": 'cmp5_game_smp_field_eq(g->source_type,sizeof(g->source_type),"image")',
        "bounded SMP format": 'cmp5_game_smp_field_eq(g->image_type,sizeof(g->image_type),"exfatfs")',
        "bounded SMP direct owner": "cmp5_game_smp_field_eq(cmp5_owner.title_id,sizeof(cmp5_owner.title_id),smp)",
        "require SMP for direct images": "image_title_required",
        "image owner gate checks actual file type": "cmp5_source_requires_smp(r.source, &cmp5_source_snapshot)",
        "SMP image ownership": '!cmp5_game_smp_field_eq(owner.path,sizeof(owner.path),r.source)',
        "SMP managed source": "!owner.managed || !owner.source_available",
        "SMP identity": '!cmp5_game_smp_field_eq(owner.title_id,sizeof(owner.title_id),r.smp_title)',
        "image owner source type": '!cmp5_game_smp_field_eq(owner.source_type,sizeof(owner.source_type),"image")',
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
    # The converter's source-type predicate lives in the COPIED native
    # header, not in the HTTP implementation. Audit the exact header
    # installed in the engine before building its PS5 binary.
    source_start = game_paths.find("static inline bool cmp5_game_path_stat(")
    source_end = game_paths.find("static inline bool cmp5_game_path_on_disk(",
                                 source_start)
    if source_start < 0 or source_end < 0 or not (
        "cmp5_convertible_image_suffix(path)"
        in game_paths[source_start:source_end]
    ):
        missing.append("regular game files must have convertible image suffix")
    # Pause and scheduler must share the same identity test. A stale
    # front-end status poll is insufficient authorization for global pause.
    if not all(part in job for part in (
        'int cmp5_job_pause_dest(const char *dest)',
        'strcmp(g_status.dest,dest)==0',
        'cmp5_game_resume_path_still_same("/",dest,&before)',
        'pthread_mutex_lock(&g_mx)',
        'if (own_job) g_pause=1',
    )):
        missing.append("pause does not bind the active scheduler to its destination")
    if "cmp5_job_pause_dest(pause_dest)" not in http:
        missing.append("httpd still exposes an unscoped pause")
    if "cmp5_game_source_allowed(path)" not in game_paths[source_start:source_end]:
        missing.append("converter source still requires the lexical allowlist")
    # The worker creates the output and its checkpoint using mkdirat from
    # a no-follow descriptor to the admitted storage root. Ordinary
    # string-based mkdir() remains only in upstream host-only fixtures.
    worker_start=job.find("static void run_job(")
    worker_end=job.find("int job_enqueue(",worker_start)
    worker=job[worker_start:worker_end] if worker_start>=0 and worker_end>=0 else ""
    if ('cmp5_game_output_create_unique("/", req.dest_base, req.name'
            not in worker or
        'cmp5_game_resume_path_still_same("/", dest, &cmp5_created_output)'
            not in worker):
        missing.append("worker missing anchored exclusive title output")
    checkpoint_reader=job[job.find("static int stage_get("):
                          job.find("/* ------------------------------------------------------------------ run -- */")]
    for fragment in ('cmp5_game_open_checkpoint_dir("/",state_dir)',
                     'openat(dir,"stage",O_RDONLY|O_NOFOLLOW|O_NONBLOCK)',
                     'cmp5_game_same_stat(&first,&named)',
                     'first.st_size==4||first.st_size==6'):
        if fragment not in checkpoint_reader:
            missing.append("resume stage reader missing strict checkpoint protection")
            break
    if 'if (stage_get(state_dir, stage, sizeof stage) != 0)' not in worker:
        missing.append("resume must reject missing or altered stage")
    create_start=game_paths.find("static inline bool cmp5_game_output_create_unique(")
    create_end=game_paths.find("static inline bool cmp5_game_resume_path_stat(",create_start)
    create=game_paths[create_start:create_end] if create_start>=0 and create_end>=0 else ""
    if ("mkdirat(fd,title_id,0755)" not in create or
        'mkdirat(child,".e2a-state",0700)' not in create or
        "O_DIRECTORY|O_NOFOLLOW" not in create):
        missing.append("new title and private checkpoint directories must be anchored and use restricted modes")
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
    image_start=http.find("static void cmp5_image_resolve(")
    image_end=http.find("static void api_list(",image_start)
    if image_start<0 or image_end<0:
        missing.append("image resolver audit span absent")
    else:
        image=http[image_start:image_end]
        before=image.find('cmp5_game_path_stat("/",path,false,&st)')
        listing=image.find("smp_list_games(")
        after=image.find('cmp5_game_path_stat("/",path,false,&after)')
        if before<0 or listing<0 or after<0 or not before<listing<after:
            missing.append("image lookup must anchor before and after SMP inventory")
        if "lstat(" in image:
            missing.append("image lookup still uses unanchored lstat")
    start=http.find('else if (strcmp(path, "/api/check") == 0)')
    end=http.find('else if (strcmp(path, "/api/settings") == 0)',start)
    if start<0 or end<0:
        missing.append("read-only preflight audit span absent")
    else:
        block=http[start:end]
        anchor=block.find('cmp5_game_path_stat("/",p,false,&cmp5_check_source)')
        owner=block.find("smp_game_info(smp,&cmp5_owner)")
        execute=block.find("job_preflight(p,smp,dst")
        final=block.find('cmp5_game_path_still_same("/",p,false,&cmp5_check_source)')
        if anchor<0 or owner<0 or execute<0 or final<0 or not anchor<owner<execute<final:
            missing.append("preflight SMP ownership/identity checks out of order")
    if 'form_get(body, "name", r.name' in http:
        missing.append("caller-selected output directory still accepted")
    if '#ifdef E2A_HOST\n    if (e2a_mkdir_p(req.dest_base) != 0)' not in job:
        missing.append("host-only parent setup for reliability test")
    if 'if (mkdir(dest, 0777) != 0)' not in job or (
        'created = true;\n    if (e2a_mkdir_p(state_dir) != 0)' not in job):
        missing.append("new output title must be created exclusively before state")
    # The worker can only remove its own .e2a-state after success.
    # Under no circumstances may a cancelled/failed conversion recursively
    # remove a destination name that a concurrent process could replace.
    cancel_start = job.find('e2a_notify("exfat2ampr: cancelled");')
    done_marker = job.find('  if (have_prof) e2a_profile_free(&prof);', cancel_start)
    if cancel_start < 0 or done_marker < 0:
        missing.append("cannot locate cancellation/error cleanup section")
    else:
        cleanup_section = job[cancel_start:done_marker]
        if "e2a_rm_rf(" in cleanup_section:
            missing.append("destructive cancellation or error cleanup remains")
        if "cancelled: preserving private conversion output" not in cleanup_section or (
            "failed: preserving conversion output" not in cleanup_section):
            missing.append("missing cancellation and error preservation branches")
    # Resume and retry bypass the POST parser. Inspect the actual patched
    # job_enqueue function so a compromised checkpoint cannot re-enable
    # original deletion, overwrites, force or arbitrary output placement.
    queue_start = job.find("int job_enqueue(const job_request *req) {")
    queue_end = job.find("int job_resume_level(", queue_start)
    if queue_start < 0 or queue_end < 0:
        missing.append("cannot inspect job enqueue and resume boundary")
    else:
        queue = job[queue_start:queue_end]
        for label, fragment in {
            "queue always preserves source": "normalized.remove_source = false;",
            "queue forbids overwrite": "normalized.overwrite = false;",
            "queue forbids force": "normalized.force = false;",
            "queue disables automated pruning": "normalized.clean_leftovers = false;",
            "queue enforces verification": "normalized.verify = true;",
            "queue enforces deduplication": "normalized.dedup = true;",
            "queue disables disk spill cache": "normalized.cache_mode = 0;",
            "queue source descriptor check": 'cmp5_game_path_stat("/", normalized.source, false, &source_identity)',
            "queue destination descriptor check": 'cmp5_game_path_on_disk("/", normalized.dest_base, true)',
            "queue rejects stale saved title": 'strcmp(normalized.name, trusted_id) != 0',
            "queue revalidates SMP owner": 'smp_game_info(normalized.smp_title, &owner) != 0',
            "queue rechecks SMP source": '!cmp5_game_smp_field_eq(owner.path,sizeof(owner.path),normalized.source)',
            "queue rechecks SMP title": '!cmp5_game_smp_field_eq(owner.title_id,sizeof(owner.title_id),normalized.smp_title)',
            "queue requires managed available image": '!owner.managed || !owner.source_available',
            "queue requires image owner": '!cmp5_game_smp_field_eq(owner.source_type,sizeof(owner.source_type),"image")',
            "queue excludes compressed PFSC": '!cmp5_game_smp_field_eq(owner.image_type,sizeof(owner.image_type),"exfatfs")',

            "queue derives title ID": 'snprintf(normalized.name, sizeof normalized.name, "%s", trusted_id);',
        }.items():
            if fragment not in queue:
                missing.append(label)
        if "#ifndef E2A_HOST" not in queue or "#endif" not in queue:
            missing.append("missing isolated PS5 queue admission contract")
    # The saved request must not be read from arbitrary user/system paths.
    resume_start = job.find("int job_resume_level(")
    resume_end = job.find("int job_resume(", resume_start)
    if resume_start < 0 or resume_end < 0:
        missing.append("cannot inspect resume checkpoint reader")
    else:
        resume = job[resume_start:resume_end]
        before = resume.find('cmp5_game_resume_path_stat("/", dest, &cmp5_resume_snapshot)')
        read = resume.find("request_load(state_dir, &r)")
        after = resume.find('cmp5_game_resume_path_still_same(')
        if before < 0 or read < 0 or after < 0 or not before < read < after:
            missing.append("checkpoint read not guarded by anchored resume identity")
    # Saved request must be a single regular file under a real state
    # directory, not a symlink or a switched leaf after its initial read.
    load_start = job.find("static int request_load(")
    load_end = job.find("static int stage_set(", load_start)
    if load_start < 0 or load_end < 0:
        missing.append("cannot inspect saved request reader")
    else:
        reader = job[load_start:load_end]
        for label, fragment in {
            "checkpoint directory nofollow": "open(state_dir,O_RDONLY|O_DIRECTORY|O_NOFOLLOW)",
            "request descriptor nofollow": 'openat(checkpoint_dir,"request.bin",O_RDONLY|O_NOFOLLOW|O_NONBLOCK)',
            "request rejects hardlinks": "request_before.st_nlink!=1",
            "request rejects switched leaf": 'fstatat(checkpoint_dir,"request.bin",&request_named,',
            "request detects changed bytes": "cmp5_game_same_stat(&request_before,&request_after)",
            "request checks named inode": "cmp5_game_same_stat(&request_before,&request_named)",
            "request checks state directory": "cmp5_game_same_stat(&checkpoint_dir_before,&checkpoint_dir_after)",
        }.items():
            if fragment not in reader:
                missing.append(label)
        if "int fd=open(path,O_RDONLY);" not in reader or "#else" not in reader:
            missing.append("host-only checkpoint fixture path not isolated")
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
