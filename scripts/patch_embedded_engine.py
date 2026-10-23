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
    # All administrative destructive routes remain disabled in the
    # embedded distribution until each is separately proven reversible.
    marker = '  if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)\n'
    guard = '''  /* Modified CleanMyPS5 engine: deny destruction and replacement. */
  if (post && (!strcmp(path, "/api/delete-source") ||
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
