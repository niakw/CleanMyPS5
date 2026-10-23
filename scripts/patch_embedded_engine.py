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
    change(httpd, '  r.overwrite = flag(body, "overwrite", false);',
           '  r.overwrite = false; /* CleanMyPS5 never overwrites game source */')
    change(httpd, '  r.remove_source = flag(body, "remove", false);',
           '  r.remove_source = false; /* no source deletion */')
    change(httpd, '  r.force = flag(body, "force", false);',
           '  r.force = false; /* no incompatible game bypass */')
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
    # Match an actual file image to the single ShadowMountPlus-managed
    # title ID; do NOT derive IDs from filenames, or mount an unknown image.
    # This is a read-only lookup. It refuses ambiguous/truncated inventories.
    image_resolve = r"""/* CleanMyPS5: read-only SMP identity mapping of game images. */
static void cmp5_image_resolve(int fd, const char *args) {
  char path[600];
  if (form_get(args, "path", path, sizeof path) < 1 ||
      path[0] != '/' || strstr(path, "..")) {
    send_json(fd,400,"{\\"error\\":\\"bad_path\\"}");return;
  }
  const char *suffix=strrchr(path,'.');
  if (!suffix ||
      (strcasecmp(suffix,".exfat")&&strcasecmp(suffix,".ffpkg")&&
       strcasecmp(suffix,".ffpfs")&&strcasecmp(suffix,".ffpfsc"))) {
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
  bool valid=tidlen==9&&g->managed&&g->source_available;
  for(size_t i=0;i<tidlen&&valid;i++){
    const char c=g->title_id[i];
    valid=i<4?(c>='A'&&c<='Z'):(c>='0'&&c<='9');
  }
  /* Compressed PFSC sources should not be recompressed blindly. */
  const bool type_ok=!strcmp(g->image_type,"exfatfs")||
       !strcmp(g->image_type,"ufs")||!strcmp(g->image_type,"pfs");
  struct stat after;
  if(!valid||!type_ok||lstat(path,&after)!=0||
     !S_ISREG(after.st_mode)||after.st_nlink!=1||
     after.st_dev!=st.st_dev||after.st_ino!=st.st_ino||
     after.st_size!=st.st_size||after.st_mtime!=st.st_mtime||
     after.st_ctime!=st.st_ctime){
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
