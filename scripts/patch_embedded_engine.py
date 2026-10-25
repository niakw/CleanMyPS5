#!/usr/bin/env python3
"""Build-only rebranding/hardening of pinned exFAT2AMPR engine.

Copyright (C) 2026 V-10 for original engine portions, GPL-3.0-or-later
with section-7 additional terms in upstream NOTICE.
CleanMyPS5 modifications (2026-10-23) are GPL-3.0-or-later.
This runs on a temporary upstream checkout, NOT against live PS5 files.
"""
from pathlib import Path
import sys

def change(path: Path, before: str, after: str):
    text = path.read_text()
    if text.count(before) != 1:
        raise SystemExit(f"Unexpected upstream source layout: {path.name} ({before[:64]!r})")
    path.write_text(text.replace(before, after))

def main(root: Path):
    common = root / "src/common.h"
    change(common, '#define E2A_STATE_DIR "/data/exfat2ampr"',
           '#define E2A_STATE_DIR "/data/cleanmyps5/compression"')
    change(common, '#define E2A_HTTP_PORT 6972', '#define E2A_HTTP_PORT 6973')

    mainc = root / "src/main.c"
    # Preserve source but mark this now-unused legacy cleanup helper.
    change(mainc,
           'static void clean_stray_mount_dirs(void) {',
           'static void __attribute__((unused)) clean_stray_mount_dirs(void) {')
    change(mainc, '  clean_stray_mount_dirs();',
           '  /* CleanMyPS5: no unsolicited deletion of mount roots on boot. */')
    change(mainc, '  e2a_mkdir_p(E2A_HOMEBREW_DIR);',
           '  /* CleanMyPS5: do not create game destination on boot. */')
    change(mainc, '  plugins_start_background();   /* ShadowMountPlus\' emulator folder, then every game\'s fakelib */',
           '  /* CleanMyPS5: no unsolicited plugin/file updates on boot. */')
    change(mainc, '  covers_start();               /* every title\'s cover kept at hand, the slow ones fetched patiently */',
           '  /* CleanMyPS5: use title UI; engine does not fetch covers at boot. */')
    change(mainc, '  e2a_profile_import_usb();',
           '  /* CleanMyPS5: no auto-import from USB at boot. */')
    change(mainc, 'static void auto_queue(void) {',
           'static void __attribute__((unused)) auto_queue(void) {')
    change(mainc, '  if (auto_mode) auto_queue();',
           '  /* CleanMyPS5: never auto-queue conversions at boot. */')
    change(mainc, '  bool auto_mode = false;',
           '  bool auto_mode = false; /* retained for diagnostic log only */')

    # The worker creates the new title folder itself, atomically.
    # mkdir_p(state_dir) on a pre-existing path could otherwise traverse a
    # replaced/dangling symlink or a renamed directory after the HTTP check.
    # Keep the resume branch unchanged: it deliberately reuses checkpoints.
    jobc = root / "src/job.c"
    change(jobc,
           '    if (e2a_mkdir_p(state_dir) != 0) FAIL("dest_create", "cannot create destination (storage not mounted or read-only?)");',
           '''    /* CleanMyPS5: exclusive creation of a new title output. No
     * directory reuse, no symlink following, no implicit overwrite. */
    /* Host reliability fixtures start without the homebrew parent.
     * On PS5 the destination root MUST already exist and pass the
     * storage + anchored admission checks; never create it there. */
#ifdef E2A_HOST
    if (e2a_mkdir_p(req.dest_base) != 0)
      FAIL("dest_unavailable", "host destination root is unavailable");
#endif
#ifdef E2A_HOST
    if (mkdir(dest, 0777) != 0)
      FAIL("dest_exists_or_unavailable", "destination is occupied or unavailable");
    created = true;
    if (e2a_mkdir_p(state_dir) != 0)
      FAIL("dest_create", "cannot initialize private output state");
#else
    struct stat cmp5_created_output={0};
    if (!cmp5_game_path_on_disk("/", req.source, false) ||
        !cmp5_game_output_create_unique("/", req.dest_base, req.name,
                                        &cmp5_created_output))
      FAIL("dest_exists_or_unavailable", "destination is occupied or unavailable");
    created = true;
    if (!cmp5_game_resume_path_still_same("/", dest, &cmp5_created_output))
      FAIL("dest_changed", "created output was renamed or replaced");
#endif''')

    # A local process could swap the title output directory AFTER a failed
    # worker stopped. The upstream cancellation/error handlers recursively
    # remove "dest" by its path (not an anchored inode), so they can delete
    # somebody else's directory. Preserve partial output for audit/resume.
    change(jobc,
           '    if (dest[0] && (created || req.resume)) e2a_rm_rf(dest);',
           '''    /* CleanMyPS5: a cancellation is NOT an authorization to delete
     * a path that may have been replaced while the worker was running.
     * Preserve the partial output for audit or an explicit resume. */
    if (dest[0] && (created || req.resume))
      e2a_log("cancelled: preserving private conversion output %s", dest);''')
    change(jobc,
           '      if (dest[0] && created && e2a_is_dir(dest)) e2a_rm_rf(dest);',
           '''      /* Never recursively delete an output after error: the path
       * might now refer to another game's directory or a symlink. */
      if (dest[0] && created)
        e2a_log("failed: preserving conversion output %s", dest);''')

    # A missing, unreadable, aliased or malformed checkpoint stage on PS5
    # is NOT an instruction to restart "pack" in a partially converted tree.
    # Resolve both the title output and its .e2a-state via O_NOFOLLOW and
    # read a single exact stage marker, including stable inode checks.
    change(jobc,
           '''static void stage_get(const char *state_dir, char *out, size_t cap) {
  char path[700], *txt = NULL;
  snprintf(path, sizeof path, "%s/stage", state_dir);
  snprintf(out, cap, "pack");
  if (e2a_read_text(path, &txt, NULL, 32) == 0) { snprintf(out, cap, "%s", txt); free(txt); }
}''',
           '''static int stage_get(const char *state_dir, char *out, size_t cap) {
#ifdef E2A_HOST
  char path[700], *txt = NULL;
  snprintf(path, sizeof path, "%s/stage", state_dir);
  snprintf(out, cap, "pack");
  if (e2a_read_text(path, &txt, NULL, 32) == 0) {
    snprintf(out, cap, "%s", txt);free(txt);
  }
  return 0;
#else
  if(!out||cap<7u)return -1;
  out[0]=0;
  int dir=cmp5_game_open_checkpoint_dir("/",state_dir);
  if(dir<0)return -1;
  int fd=openat(dir,"stage",O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
  if(fd<0){close(dir);return -1;}
  struct stat first={0},last={0},named={0};
  int good=fstat(fd,&first)==0&&S_ISREG(first.st_mode)&&
           first.st_nlink==1&&(first.st_size==4||first.st_size==6);
  char value[7]={0};
  size_t count=0;
  while(good&&count<(size_t)first.st_size){
    ssize_t got=read(fd,value+count,(size_t)first.st_size-count);
    if(got>0)count+=(size_t)got;
    else if(got<0&&errno==EINTR)continue;
    else good=0;
  }
  char extra=0;
  if(good&&read(fd,&extra,1)!=0)good=0;
  if(good){
    const bool known=(count==4u&&
         (!memcmp(value,"pack",4)||!memcmp(value,"copy",4)))||
         (count==6u&&!memcmp(value,"verify",6));
    good=known&&fstat(fd,&last)==0&&
      fstatat(dir,"stage",&named,AT_SYMLINK_NOFOLLOW)==0&&
      cmp5_game_same_stat(&first,&last)&&
      cmp5_game_same_stat(&first,&named);
    if(good){
      int current_dir=cmp5_game_open_checkpoint_dir("/",state_dir);
      if(current_dir<0)good=0;
      else{
        struct stat held_state={0},current_state={0};
        good=fstat(dir,&held_state)==0&&
             fstat(current_dir,&current_state)==0&&
             held_state.st_dev==current_state.st_dev&&
             held_state.st_ino==current_state.st_ino;
        close(current_dir);
      }
    }
  }
  close(fd);close(dir);
  if(!good)return -1;
  memcpy(out,value,count);out[count]=0;
  return 0;
#endif
}''')
    # Patch the six-space inventory call first: the four-space call is a
    # substring of it, and the patcher intentionally demands unique anchors.
    change(jobc,
           '      stage_get(state_dir, stage, sizeof stage);',
           '''      if (stage_get(state_dir, stage, sizeof stage) != 0) continue;''')
    change(jobc,
           '    stage_get(state_dir, stage, sizeof stage);',
           '''    if (stage_get(state_dir, stage, sizeof stage) != 0)
      FAIL("state_invalid", "missing, swapped or malformed resume stage");''')

    # A RESUME or RETRY rehydrates a job_request from .e2a-state rather than
    # parsing /api/start. Pin every dangerous field again at the *last*
    # enqueue boundary, and derive a trusted Title ID from source metadata.
    # Existing saved names must match: reject, never silently redirect.
    change(jobc, '#include "job.h"',
           '''#include "job.h"
#ifndef E2A_HOST
#include "cmp5_compression_allowed_paths.h"
#endif''')
    # The controller's initial progress read is NOT an authorization to
    # pause whichever job happens to be active later. Validate the exact
    # output directory under the same scheduler mutex used by the worker.
    change(jobc, 'void job_pause(void) { g_pause = 1; }',
           '''void job_pause(void) { g_pause = 1; }
#ifndef E2A_HOST
int cmp5_job_pause_dest(const char *dest) {
  struct stat before={0};
  if (!dest || !cmp5_game_resume_path_stat("/",dest,&before)) return -1;
  pthread_mutex_lock(&g_mx);
  const bool own_job=g_status.state==JOB_RUNNING &&
      strcmp(g_status.dest,dest)==0 &&
      cmp5_game_resume_path_still_same("/",dest,&before);
  if (own_job) g_pause=1;
  pthread_mutex_unlock(&g_mx);
  return own_job?0:-1;
}
#endif''')
    jobh=root / "src/job.h"
    change(jobh, 'void job_pause(void);                       /* checkpoint and stop the current job; resume later */',
           '''void job_pause(void);                       /* checkpoint and stop the current job; resume later */
#ifndef E2A_HOST
/* Pause only the exact verified output of the actively running PS5 job. */
int cmp5_job_pause_dest(const char *dest);
#endif''')
    change(jobc,
           '  job_request normalized = *req;\n  if (!normalized.dest_base[0])',
           '''  job_request normalized = *req;
#ifndef E2A_HOST
  /* Never trust flags or output paths from an old/fabricated checkpoint. */
  normalized.clean_leftovers = false;
  normalized.overwrite = false;
  normalized.remove_source = false;
  normalized.force = false;
  normalized.verify = true;
  normalized.dedup = true;
  normalized.cache_mode = 0;
  normalized.test_pause_after = 0;
  if (!normalized.dest_base[0])
    snprintf(normalized.dest_base, sizeof normalized.dest_base, "%s",
             E2A_HOMEBREW_DIR);
  struct stat source_identity = {0};
  if (!cmp5_game_path_stat("/", normalized.source, false, &source_identity) ||
      !cmp5_game_path_on_disk("/", normalized.dest_base, true))
    return -1;
  char trusted_id[16] = {0};
  if (normalized.smp_title[0]) {
    smp_game owner = {0};
    if (!S_ISREG(source_identity.st_mode) ||
        strlen(normalized.smp_title) != 9 ||
        smp_game_info(normalized.smp_title, &owner) != 0 ||
        !owner.managed || !owner.source_available ||
        !cmp5_game_smp_field_eq(owner.path,sizeof(owner.path),normalized.source) ||
        !cmp5_game_smp_field_eq(owner.title_id,sizeof(owner.title_id),normalized.smp_title) ||
        !cmp5_game_smp_field_eq(owner.source_type,sizeof(owner.source_type),"image") ||
        (!cmp5_game_smp_field_eq(owner.image_type,sizeof(owner.image_type),"exfatfs") &&
         !cmp5_game_smp_field_eq(owner.image_type,sizeof(owner.image_type),"ufs") &&
         !cmp5_game_smp_field_eq(owner.image_type,sizeof(owner.image_type),"pfs")))
      return -1;
    snprintf(trusted_id, sizeof trusted_id, "%s", normalized.smp_title);
  } else {
    if (!S_ISDIR(source_identity.st_mode)) return -1;
    e2a_game verified_game;
    if (e2a_read_game(normalized.source, &verified_game) != 0) return -1;
    snprintf(trusted_id, sizeof trusted_id, "%s", verified_game.title_id);
  }
  if (strlen(trusted_id) != 9) return -1;
  for (unsigned i = 0; i < 9; i++) {
    const char c = trusted_id[i];
    if (i < 4 ? (c < 'A' || c > 'Z') : (c < '0' || c > '9'))
      return -1;
  }
  if (normalized.name[0] && strcmp(normalized.name, trusted_id) != 0)
    return -1; /* a resume filename may not redirect its output */
  snprintf(normalized.name, sizeof normalized.name, "%s", trusted_id);
#endif
  if (!normalized.dest_base[0])''')

    # A resume reads a checkpoint BEFORE job_enqueue can reject paths.
    # Require a real allowlisted output directory on PS5 before that read.
    change(jobc,
           '  job_request r;\n  if (request_load(state_dir, &r) != 0) return -1;',
           '''#ifndef E2A_HOST
  struct stat cmp5_resume_snapshot = {0};
  if (!cmp5_game_resume_path_stat("/", dest, &cmp5_resume_snapshot))
    return -1;
#endif
  job_request r;
  if (request_load(state_dir, &r) != 0) return -1;
#ifndef E2A_HOST
  if (!cmp5_game_resume_path_still_same(
        "/", dest, &cmp5_resume_snapshot)) return -1;
#endif''')

    # The upstream resume reader used open(path), following a possible
    # .e2a-state or request.bin symlink even after the outer directory was
    # validated. Reopen the checkpoint privately by descriptor and verify
    # the current directory entry still points to the read file.
    change(jobc,
           '  int fd = open(path, O_RDONLY);\n  if (fd < 0) return -1;',
           '''#ifndef E2A_HOST
  int checkpoint_dir=open(state_dir,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  if(checkpoint_dir<0)return -1;
  struct stat checkpoint_dir_before={0};
  if(fstat(checkpoint_dir,&checkpoint_dir_before)!=0 ||
     !S_ISDIR(checkpoint_dir_before.st_mode)){
    close(checkpoint_dir);return -1;
  }
  int fd=openat(checkpoint_dir,"request.bin",O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
#else
  int fd=open(path,O_RDONLY);
#endif
  if(fd<0){
#ifndef E2A_HOST
    close(checkpoint_dir);
#endif
    return -1;
  }
#ifndef E2A_HOST
  struct stat request_before={0};
  if(fstat(fd,&request_before)!=0 ||
     !S_ISREG(request_before.st_mode)||request_before.st_nlink!=1 ||
     (request_before.st_size!=(off_t)sizeof *r &&
      request_before.st_size!=(off_t)offsetof(job_request,force))){
    close(fd);close(checkpoint_dir);return -1;
  }
#endif''')
    change(jobc,
           '  close(fd);\n  const size_t boolean_fields[]',
           '''#ifndef E2A_HOST
  struct stat request_after={0},request_named={0},checkpoint_dir_after={0};
  if(fstat(fd,&request_after)!=0 ||
     fstatat(checkpoint_dir,"request.bin",&request_named,
             AT_SYMLINK_NOFOLLOW)!=0 ||
     fstat(checkpoint_dir,&checkpoint_dir_after)!=0 ||
     !cmp5_game_same_stat(&request_before,&request_after) ||
     !cmp5_game_same_stat(&request_before,&request_named) ||
     !cmp5_game_same_stat(&checkpoint_dir_before,&checkpoint_dir_after))
    rc=-1;
  close(checkpoint_dir);
#endif
  close(fd);
  const size_t boolean_fields[]''')

    httpd = root / "src/httpd.c"
    change(httpd,
           'else if (strcmp(path, "/api/pause") == 0 && post) { job_pause(); send_json(fd, 200, "{\\"ok\\":true}"); }',
           '''else if (strcmp(path, "/api/pause") == 0 && post) {
#ifdef E2A_HOST
    job_pause();send_json(fd,200,"{\\"ok\\":true}");
#else
    char pause_dest[600]={0};
    /* No unscoped pause: even a local caller must supply the exact
     * destination; a different title cannot be interrupted between
     * polling /api/cmp5-progress and this mutation. */
    if(!local || form_get(args,"dest",pause_dest,sizeof pause_dest)<1 ||
       !SAFE_PATH(pause_dest) ||
       cmp5_job_pause_dest(pause_dest)!=0)
      send_json(fd,409,"{\\"error\\":\\"pause_target_changed\\"}");
    else send_json(fd,200,"{\\"ok\\":true}");
#endif
  }''')
    change(httpd, 'addr.sin_addr.s_addr = htonl(INADDR_ANY);',
           'addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);')
    # All request flags are hard-pinned in our distribution. Keep the
    # unused upstream helper available but quiet under -Werror.
    change(httpd, 'static bool flag(const char *body, const char *key, bool dflt) {',
           'static bool __attribute__((unused)) flag(const char *body, const char *key, bool dflt) {')
    # The pinned upstream accepts an optional user-supplied 'name'.
    # Never trust it for output placement: otherwise a local HTTP client
    # can choose a different directory than the verified SMP title ID.
    # job_enqueue() derives the folder name from the owned SMP ID or
    # e2a_read_game(source), including for ordinary homebrew folders.
    change(httpd, '  form_get(body, "name", r.name, sizeof r.name);',
           '  r.name[0] = 0; /* derive from verified game identity only */')
    change(httpd, '  r.overwrite = flag(body, "overwrite", false);',
           '  r.overwrite = false; /* CleanMyPS5 never overwrites game source */')
    change(httpd, '  r.remove_source = flag(body, "remove", false);',
           '  r.remove_source = false; /* no source deletion */')
    change(httpd, '  r.force = flag(body, "force", false);',
           '  r.force = false; /* no incompatible game bypass */')
    change(httpd, '  r.clean_leftovers = flag(body, "clean", true);',
           '  r.clean_leftovers = false; /* never prune unrelated files */')
    change(httpd, '  r.verify = flag(body, "verify", true);',
           '  r.verify = true; /* mandatory output integrity verification */')
    change(httpd, '  r.dedup = flag(body, "dedup", true);',
           '  r.dedup = true; /* preserve tested packer behavior */')
    change(httpd, '  r.cache_mode = form_get(body, "cache", v, sizeof v) >= 0 && v[0] == \'1\' ? 1 : 0;',
           '  r.cache_mode = 0; /* no optional cache on other volumes */')
    change(httpd,
           '  if (r.dest_base[0] && !SAFE_PATH(r.dest_base)) { send_json(fd, 400, "{\\"error\\":\\"bad destination\\",\\"code\\":\\"dest_unavailable\\"}"); return; }',
           '''  if (r.dest_base[0] && !SAFE_PATH(r.dest_base)) { send_json(fd, 400, "{\\"error\\":\\"bad destination\\",\\"code\\":\\"dest_unavailable\\"}"); return; }
  /* Direct API calls must use exactly the game paths inventoried by
   * CleanMyPS5, not arbitrary data, saves or system directories. */
  if (!cmp5_game_source_allowed(r.source) ||
      !cmp5_game_destination_allowed(r.dest_base)) {
    send_json(fd, 400, "{\\"error\\":\\"game_path_not_allowed\\"}");
    return;
  }
  /* A permitted name must ALSO resolve through real directories.
   * This rejects a homebrew/ or USB symlink alias of savedata/system
   * before a separate local client can enqueue conversion work. */
  struct stat cmp5_source_snapshot, cmp5_dest_snapshot;
  if (!cmp5_game_path_stat("/", r.source, false, &cmp5_source_snapshot) ||
      !cmp5_game_path_stat("/", r.dest_base, true, &cmp5_dest_snapshot)) {
    send_json(fd, 409, "{\\"error\\":\\"game_path_unstable_or_aliased\\"}");
    return;
  }
  /* An SMP image must not reuse or overwrite a title output, even if
   * the local HTTP client submitted overwrite=0. */
  if (r.smp_title[0] &&
      !cmp5_game_output_vacant("/", r.dest_base, r.smp_title)) {
    send_json(fd, 409, "{\\"error\\":\\"game_output_already_present\\"}");
    return;
  }''')
    # Revalidate managed image ownership INSIDE the engine immediately
    # before enqueue. GUI checks alone cannot authorize a filesystem job:
    # they can race with ShadowMountPlus or another loopback client.
    # Folder jobs (without smp=) retain their existing upstream path.
    change(httpd,
           '  (void)queue_only;',
           '''  /* CleanMyPS5: image paths require a real ShadowMountPlus
   * title. A local client must not bypass checks by omitting smp=. */
  /* Extracted game directories may be named *.exfat or *.ffpkg.
   * Only regular image files need an SMP owner. Game directories still
   * require e2a_read_game() and an independently verified Title ID. */
  if (!r.smp_title[0] &&
      cmp5_source_requires_smp(r.source, &cmp5_source_snapshot)) {
    send_json(fd, 409, "{\\"error\\":\\"image_title_required\\"}");
    return;
  }
  /* CleanMyPS5: an SMP image must still have its exact
   * owner and the original path at the time of queue submission. */
  if (r.smp_title[0]) {
    smp_game owner;
    struct stat source_st;
    const char *ext = strrchr(r.source, '.');
    const bool supported = ext &&
      (!strcasecmp(ext, ".exfat") || !strcasecmp(ext, ".ffpkg") ||
       !strcasecmp(ext, ".ffpfs"));
    if (!supported ||
        smp_game_info(r.smp_title, &owner) != 0 ||
        !cmp5_game_smp_field_eq(owner.title_id,sizeof(owner.title_id),r.smp_title) ||
        !cmp5_game_smp_field_eq(owner.path,sizeof(owner.path),r.source) ||
        !cmp5_game_smp_field_eq(owner.source_type,sizeof(owner.source_type),"image") ||
        !owner.managed || !owner.source_available ||
        (cmp5_game_smp_field_eq(owner.image_type,sizeof(owner.image_type),"pfsc") ||
         (!cmp5_game_smp_field_eq(owner.image_type,sizeof(owner.image_type),"exfatfs") &&
          !cmp5_game_smp_field_eq(owner.image_type,sizeof(owner.image_type),"ufs") &&
          !cmp5_game_smp_field_eq(owner.image_type,sizeof(owner.image_type),"pfs"))) ||
        lstat(r.source, &source_st) != 0 ||
        !S_ISREG(source_st.st_mode) || source_st.st_nlink != 1 ||
        source_st.st_size <= 0) {
      send_json(fd, 409, "{\\"error\\":\\"image_identity_changed\\"}");
      return;
    }
    /* Refuse image replacement between UI approval and engine enqueue. */
    char expected_text[32] = "";
    if (form_get(body, "cmp5_size", expected_text,
                 sizeof expected_text) <= 0 ||
        expected_text[0] < '1' || expected_text[0] > '9') {
      send_json(fd, 409, "{\\"error\\":\\"image_size_required\\"}");
      return;
    }
    uint64_t expected = 0;
    for (const char *digit = expected_text; *digit; digit++) {
      if (*digit < '0' || *digit > '9') {
        send_json(fd, 409, "{\\"error\\":\\"invalid_image_size\\"}");
        return;
      }
      const unsigned d = (unsigned)(*digit - '0');
      if (expected > (UINT64_MAX - d) / 10u) {
        send_json(fd, 409, "{\\"error\\":\\"invalid_image_size\\"}");
        return;
      }
      expected = expected * 10u + d;
    }
    if ((uint64_t)source_st.st_size != expected) {
      send_json(fd, 409, "{\\"error\\":\\"image_size_changed\\"}");
      return;
    }
    /* The embedded worker cannot rely on the frontend's earlier GET.
     * The same SOURCE/DESTINATION must pass the upstream live preflight
     * immediately before enqueuing. An unmounted image is 'later', not yes.
     * The preflight JSON comes from our own engine, not the HTTP client. */
    char preflight_json[4096];
    if (job_preflight(r.source, r.smp_title, r.dest_base,
                      preflight_json, sizeof preflight_json) != 0 ||
        !strstr(preflight_json, "\\"compatible\\":\\"yes\\"") ||
        !strstr(preflight_json, "\\"errors\\":[]") ||
        !strstr(preflight_json, "\\"warnings\\":[]")) {
      send_json(fd, 409, "{\\"error\\":\\"image_preflight_required\\"}");
      return;
    }
    /* Preflight can mount/read the image and takes time. A file modified,
     * replaced or hardlinked during that operation must never be queued
     * against the stale identity/space decision. */
    struct stat verified_st;
    if (lstat(r.source, &verified_st) != 0 ||
        !S_ISREG(verified_st.st_mode) || verified_st.st_nlink != 1 ||
        verified_st.st_dev != source_st.st_dev ||
        verified_st.st_ino != source_st.st_ino ||
        verified_st.st_mode != source_st.st_mode ||
        verified_st.st_size != source_st.st_size ||
        verified_st.st_mtime != source_st.st_mtime ||
        verified_st.st_ctime != source_st.st_ctime ||
#if defined(__APPLE__)
        verified_st.st_mtimespec.tv_nsec != source_st.st_mtimespec.tv_nsec ||
        verified_st.st_ctimespec.tv_nsec != source_st.st_ctimespec.tv_nsec ||
#else
        verified_st.st_mtim.tv_nsec != source_st.st_mtim.tv_nsec ||
        verified_st.st_ctim.tv_nsec != source_st.st_ctim.tv_nsec ||
#endif
        (uint64_t)verified_st.st_size != expected) {
      send_json(fd, 409, "{\\"error\\":\\"image_mutated_during_preflight\\"}");
      return;
    }
  }
  /* Repeat the anchored admission immediately before enqueue. An SMP
   * preflight may have taken long enough for source/destination aliases
   * to be replaced. This is NOT a filesystem transaction or lock. */
  if (!cmp5_game_path_still_same("/", r.source, false,
                               &cmp5_source_snapshot) ||
      !cmp5_game_path_still_same("/", r.dest_base, true,
                               &cmp5_dest_snapshot)) {
    send_json(fd, 409, "{\\"error\\":\\"game_identity_changed_during_preflight\\"}");
    return;
  }
  /* Recheck occupancy after the potentially long SMP preflight. */
  if (r.smp_title[0] &&
      !cmp5_game_output_vacant("/", r.dest_base, r.smp_title)) {
    send_json(fd, 409, "{\\"error\\":\\"game_output_occupied_before_enqueue\\"}");
    return;
  }
  (void)queue_only;''')
    # Stable, compact endpoint for the bundled DualSense frontend. The
    # upstream /api/state may include multi-megabyte logs; don't parse or
    # expose those in the title process.
    progress = """/* CleanMyPS5 modified AMPR service, Copyright (C) 2026 V-10:
 * one read-only bounded status record for the native UI. */
static void cmp5_progress(int fd) {
  job_status status;
  job_get_status(&status);
  static const char *const names[]={
    "idle","running","done","error","cancelled","paused"};
  int index=(int)status.state;
  if(index<0 || index>=6)index=3;
  int pct=status.percent;
  if(pct<0)pct=0;
  if(pct>100)pct=100;
  char out[256];
  snprintf(out,sizeof out,
    "{\\"service\\":\\"CleanMyPS5-AMPR\\",\\"state\\":\\"%s\\","
    "\\"percent\\":%d,\\"bytes_done\\":%llu,\\"bytes_total\\":%llu,"
    "\\"queue_len\\":%d,\\"launch_checked\\":%s,\\"title_id\\":\\"%.9s\\"}",
    names[index],pct,
    (unsigned long long)status.bytes_done,
    (unsigned long long)status.bytes_total,
    status.queue_len,
    status.launch_checked?"true":"false",status.title_id);
  send_json(fd,200,out);
}
"""
    change(httpd, 'static void api_list(int fd, int (*fn)(char *, size_t)) {',
           progress + 'static void api_list(int fd, int (*fn)(char *, size_t)) {')
    change(httpd,
           'else if (strcmp(path, "/api/state") == 0) api_state(fd, args);',
           '''else if (strcmp(path, "/api/state") == 0) api_state(fd, args);
   else if (strcmp(path, "/api/cmp5-progress") == 0 && !post) {
     if(!local)send_json(fd,403,"{\\"error\\":\\"local only\\"}");
     else cmp5_progress(fd);
   }''')
    change(httpd, '#include <sys/socket.h>',
           '#include <sys/socket.h>\n#include <sys/stat.h>\n#include "cmp5_compression_allowed_paths.h"')
    # Match an actual file image to the single ShadowMountPlus-managed
    # title ID; do NOT derive IDs from filenames, or mount an unknown image.
    # This is a read-only lookup. It refuses ambiguous/truncated inventories.
    image_resolve = """/* CleanMyPS5: read-only SMP identity mapping of game images. */
static void cmp5_image_resolve(int fd, const char *args) {
  char path[600];
  if (form_get(args, "path", path, sizeof path) < 1 ||
      path[0] != '/' || strstr(path, "..")) {
    send_json(fd,400,"{\\"error\\":\\"bad_path\\"}");return;
  }
  const char *suffix=strrchr(path,'.');
  if (!suffix ||
      (strcasecmp(suffix,".exfat")&&strcasecmp(suffix,".ffpkg")&&
       strcasecmp(suffix,".ffpfs"))) {
    send_json(fd,400,"{\\"error\\":\\"unsupported_image\\"}");return;
  }
  /* Lookup is only permitted in known game roots. No symlinked
   * intermediate path can redirect an otherwise legitimate query. */
  struct stat st;
  if(!cmp5_game_source_allowed(path) ||
     !cmp5_game_path_stat("/",path,false,&st) ||
     !S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<=0) {
    send_json(fd,409,"{\\"error\\":\\"unstable_source\\"}");return;
  }
  enum { LIMIT=512 };
  smp_game *items=calloc(LIMIT,sizeof(*items));
  if(!items){send_json(fd,500,"{\\"error\\":\\"memory\\"}");return;}
  const int count=smp_list_games(items,LIMIT);
  int match=-1;
  if(count>=0&&count<LIMIT)for(int i=0;i<count;i++){
    if(!cmp5_game_smp_field_eq(items[i].path,sizeof(items[i].path),path))continue;
    if(match>=0){match=-2;break;} /* multiple owners: refuse */
    match=i;
  }
  if(match<0||count==LIMIT){
    free(items);
    send_json(fd,409,"{\\"error\\":\\"not_unambiguously_managed\\"}");return;
  }
  smp_game *g=&items[match];
  /* Never read beyond a malformed nonterminated SMP title_id array. */
  const size_t tidlen=strnlen(g->title_id,sizeof(g->title_id));
  bool valid=tidlen==9&&g->managed&&g->source_available&&
             cmp5_game_smp_field_eq(g->source_type,sizeof(g->source_type),"image");
  for(size_t i=0;i<tidlen&&valid;i++){
    const char c=g->title_id[i];
    valid=i<4?(c>='A'&&c<='Z'):(c>='0'&&c<='9');
  }
  /* Compressed PFSC sources should not be recompressed blindly. */
  const bool type_ok=cmp5_game_smp_field_eq(g->image_type,sizeof(g->image_type),"exfatfs")||
       cmp5_game_smp_field_eq(g->image_type,sizeof(g->image_type),"ufs")||cmp5_game_smp_field_eq(g->image_type,sizeof(g->image_type),"pfs");
  struct stat after;
  if(!cmp5_game_path_stat("/",path,false,&after) ||
     !cmp5_game_same_stat(&st,&after)) {
    free(items);send_json(fd,409,"{\\"error\\":\\"source_changed\\"}");return;
  }
  if(!valid||!type_ok||!S_ISREG(after.st_mode)||after.st_nlink!=1){
    free(items);send_json(fd,409,"{\\"error\\":\\"image_unavailable_or_incompatible\\"}");return;
  }
  char response[180];
  snprintf(response,sizeof response,
    "{\\"title_id\\":\\"%s\\",\\"image_type\\":\\"%s\\",\\"size\\":%llu}",
    g->title_id,g->image_type,(unsigned long long)st.st_size);
  free(items);
  send_json(fd,200,response);
}
"""
    change(httpd, 'static void api_list(int fd, int (*fn)(char *, size_t)) {',
           image_resolve + 'static void api_list(int fd, int (*fn)(char *, size_t)) {')
    # Upstream /api/check is read-only but performs unbounded path-based
    # preflight. The trusted title and any other local client must obey
    # the same whitelist before touching media or storage metadata.
    change(httpd,
           '    if (job_preflight(p, smp, dst, out, sizeof out) == 0) send_json(fd, 200, out);',
           '''    struct stat cmp5_check_source={0},cmp5_check_dest={0};
    if(!cmp5_game_source_allowed(p) ||
       !cmp5_game_destination_allowed(dst) ||
       !cmp5_game_path_stat("/",p,false,&cmp5_check_source) ||
       !cmp5_game_path_stat("/",dst,true,&cmp5_check_dest) ||
       (S_ISREG(cmp5_check_source.st_mode) &&
        (!cmp5_source_requires_smp(p,&cmp5_check_source) || !smp[0] ||
         !strcasecmp(strrchr(p,'.'),".ffpfsc"))) ||
       (S_ISDIR(cmp5_check_source.st_mode) && smp[0])) {
      send_json(fd,403,"{\\"error\\":\\"game_preflight_path_not_allowed\\"}");
      free(req);return;
    }
    /* The SMP Title ID must own THIS selected image, not an unrelated
     * image handed in by another client on the loopback socket. */
    if(S_ISREG(cmp5_check_source.st_mode)){
      smp_game cmp5_owner={0};
      if(smp_game_info(smp,&cmp5_owner)!=0 ||
         !cmp5_owner.managed ||
         !cmp5_game_smp_field_eq(cmp5_owner.title_id,sizeof(cmp5_owner.title_id),smp) ||
         !cmp5_game_smp_field_eq(cmp5_owner.path,sizeof(cmp5_owner.path),p) ||
         !cmp5_game_smp_field_eq(cmp5_owner.source_type,sizeof(cmp5_owner.source_type),"image")){
        send_json(fd,409,"{\\"error\\":\\"game_preflight_owner_mismatch\\"}");
        free(req);return;
      }
    }
    const int cmp5_preflight_rc=job_preflight(p,smp,dst,out,sizeof out);
    if(!cmp5_game_path_still_same("/",p,false,&cmp5_check_source) ||
       !cmp5_game_path_still_same("/",dst,true,&cmp5_check_dest)){
      send_json(fd,409,"{\\"error\\":\\"game_preflight_source_changed\\"}");
      free(req);return;
    }
    if(cmp5_preflight_rc==0) send_json(fd,200,out);''')
    change(httpd,
           'else if (strcmp(path, "/api/check") == 0) {',
           '''else if (strcmp(path, "/api/cmp5-image") == 0 && !post) {
     if(!local)send_json(fd,403,"{\\"error\\":\\"local only\\"}");
     else cmp5_image_resolve(fd,args);
   } else if (strcmp(path, "/api/check") == 0) {''')
    # All administrative destructive routes remain disabled in the
    # embedded distribution until each is separately proven reversible.
    marker = '  if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)\n'
    guard = '''  /* The native title uses exactly three write operations: submit,
   * pause and resume. Reject every other POST even on localhost.
   * This also covers future upstream mutation endpoints by default. */
  if (post && strcmp(path, "/api/start") &&
      strcmp(path, "/api/pause") && strcmp(path, "/api/resume")) {
    send_json(fd, 403, "{\\"error\\":\\"write_operation_not_allowed\\"}");
    free(req);return;
  }
  /* Modified CleanMyPS5 engine: deny destruction and replacement. */
  if (post && (!strcmp(path, "/api/delete-source") ||
               !strcmp(path, "/api/cancel") ||
               !strcmp(path, "/api/set-aside") ||
               !strcmp(path, "/api/discard") ||
               !strcmp(path, "/api/repair") ||
               !strncmp(path, "/api/update/", 12) ||
               !strcmp(path, "/api/plugins/refresh") ||
               !strcmp(path, "/api/profile") ||
               !strcmp(path, "/api/profiles/import-usb") ||
               !strcmp(path, "/api/diagnostic"))) {
    send_json(fd, 403, "{\\"error\\":\\"disabled by CleanMyPS5 safety policy\\"}");
    free(req);return;
  }
'''
    change(httpd, marker, guard + marker)
    print("Pinned engine transformed for CleanMyPS5: loopback-only, isolated, no boot mutations")

if __name__ == "__main__":
    if len(sys.argv) != 2: raise SystemExit("usage: patch_embedded_engine.py UPSTREAM_CHECKOUT")
    main(Path(sys.argv[1]))
