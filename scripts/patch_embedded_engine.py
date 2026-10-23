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

    httpd = root / "src/httpd.c"
    change(httpd, 'addr.sin_addr.s_addr = htonl(INADDR_ANY);',
           'addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);')
    # All request flags are hard-pinned in our distribution. Keep the
    # unused upstream helper available but quiet under -Werror.
    change(httpd, 'static bool flag(const char *body, const char *key, bool dflt) {',
           'static bool __attribute__((unused)) flag(const char *body, const char *key, bool dflt) {')
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
    # Revalidate managed image ownership INSIDE the engine immediately
    # before enqueue. GUI checks alone cannot authorize a filesystem job:
    # they can race with ShadowMountPlus or another loopback client.
    # Folder jobs (without smp=) retain their existing upstream path.
    change(httpd,
           '  (void)queue_only;',
           '''  /* CleanMyPS5: an SMP image must still have its exact
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
        strcmp(owner.title_id, r.smp_title) != 0 ||
        strcmp(owner.path, r.source) != 0 ||
        strcmp(owner.source_type, "image") != 0 ||
        !owner.managed || !owner.source_available ||
        (!strcmp(owner.image_type, "pfsc") ||
         (strcmp(owner.image_type, "exfatfs") != 0 &&
          strcmp(owner.image_type, "ufs") != 0 &&
          strcmp(owner.image_type, "pfs") != 0)) ||
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
           '#include <sys/socket.h>\n#include <sys/stat.h>')
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
  struct stat st;
  if(lstat(path,&st)!=0||!S_ISREG(st.st_mode)||st.st_nlink!=1||
     st.st_size<=0) {
    send_json(fd,409,"{\\"error\\":\\"unstable_source\\"}");return;
  }
  enum { LIMIT=512 };
  smp_game *items=calloc(LIMIT,sizeof(*items));
  if(!items){send_json(fd,500,"{\\"error\\":\\"memory\\"}");return;}
  const int count=smp_list_games(items,LIMIT);
  int match=-1;
  if(count>=0&&count<LIMIT)for(int i=0;i<count;i++){
    if(strcmp(items[i].path,path)!=0)continue;
    if(match>=0){match=-2;break;} /* multiple owners: refuse */
    match=i;
  }
  if(match<0||count==LIMIT){
    free(items);
    send_json(fd,409,"{\\"error\\":\\"not_unambiguously_managed\\"}");return;
  }
  smp_game *g=&items[match];
  const size_t tidlen=strlen(g->title_id);
  bool valid=tidlen==9&&g->managed&&g->source_available&&
             !strcmp(g->source_type,"image");
  for(size_t i=0;i<tidlen&&valid;i++){
    const char c=g->title_id[i];
    valid=i<4?(c>='A'&&c<='Z'):(c>='0'&&c<='9');
  }
  /* Compressed PFSC sources should not be recompressed blindly. */
  const bool type_ok=!strcmp(g->image_type,"exfatfs")||
       !strcmp(g->image_type,"ufs")||!strcmp(g->image_type,"pfs");
  struct stat after;
  if(lstat(path,&after)!=0) {
    free(items);send_json(fd,409,"{\\"error\\":\\"source_changed\\"}");return;
  }
#if defined(__APPLE__)
  const bool subsecond_same=after.st_mtimespec.tv_nsec==st.st_mtimespec.tv_nsec&&
      after.st_ctimespec.tv_nsec==st.st_ctimespec.tv_nsec;
#else
  const bool subsecond_same=after.st_mtim.tv_nsec==st.st_mtim.tv_nsec&&
      after.st_ctim.tv_nsec==st.st_ctim.tv_nsec;
#endif
  if(!valid||!type_ok||!subsecond_same||
     !S_ISREG(after.st_mode)||after.st_nlink!=1||
     after.st_dev!=st.st_dev||after.st_ino!=st.st_ino||
     after.st_size!=st.st_size||after.st_mode!=st.st_mode||
     after.st_mtime!=st.st_mtime||after.st_ctime!=st.st_ctime){
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
    change(httpd,
           'else if (strcmp(path, "/api/check") == 0) {',
           '''else if (strcmp(path, "/api/cmp5-image") == 0 && !post) {
     if(!local)send_json(fd,403,"{\\"error\\":\\"local only\\"}");
     else cmp5_image_resolve(fd,args);
   } else if (strcmp(path, "/api/check") == 0) {''')
    # All administrative destructive routes remain disabled in the
    # embedded distribution until each is separately proven reversible.
    marker = '  if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)\n'
    guard = '''  /* Modified CleanMyPS5 engine: deny destruction and replacement. */
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
