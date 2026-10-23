#include "../src/native/compression_allowed_paths.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 const char *accepted[] = {
  "/data/homebrew/PPSA12345",
  "/data/homebrew/GAME.exfat",
  "/data/etaHEN/games/PPSA12345",
  "/mnt/usb0/homebrew/PPSA12345",
  "/mnt/ext7/etaHEN/games/A Game [US]",
  "/mnt/ext0/GAME.EXFAT",
  "/mnt/usb7/X.FFPKG",
  "/mnt/usb1/B.ffpfs",
  "/mnt/ext6/F.ffpfsc",
 };
 const char *rejected[] = {
  "/data/homebrew", "/data/homebrew/../user/app",
  "/data/homebrew/A/B", "/data/homebrew/.hidden",
  "/data/homebrew//X", "/data/homebrew/abc%2fdef",
  "/mnt/usb8/GAME.exfat", "/mnt/ext8/homebrew/GAME",
  "/mnt/ext0/GAME", "/mnt/usb0/homebrew",
  "/mnt/ext0/./GAME.exfat", "/mnt/ext0/../GAME.exfat",
  "/mnt/usb0/savedata/ABCD", "/mnt/ext0/README.md",
  "/user/savedata/X", "/data/pldmgr/payloads/X",
  "/mnt/usb0/homebrew/GAME/eboot.bin",
  "/mnt/usb0/GAME.exfat/../../sys", "", "../games/X",
 };
 for (size_t i=0;i<sizeof accepted/sizeof *accepted;++i)
  assert(cmp5_game_source_allowed(accepted[i]));
 for (size_t i=0;i<sizeof rejected/sizeof *rejected;++i)
  assert(!cmp5_game_source_allowed(rejected[i]));
 const char *dest_ok[] = {"/data/homebrew","/mnt/usb0/homebrew",
                         "/mnt/ext7/homebrew"};
 const char *dest_bad[] = {"/data","/data/etaHEN/games",
                          "/mnt/usb0","/mnt/usb8/homebrew",
                          "/mnt/ext1/homebrew/", "/mnt/ext1/homebrew/../x",
                          "/user", "/mnt/usb0/homebrew/extra", ""};
 for (size_t i=0;i<sizeof dest_ok/sizeof *dest_ok;++i)
  assert(cmp5_game_destination_allowed(dest_ok[i]));
 for (size_t i=0;i<sizeof dest_bad/sizeof *dest_bad;++i)
  assert(!cmp5_game_destination_allowed(dest_bad[i]));
 assert(!cmp5_game_source_allowed(NULL));
 assert(!cmp5_game_destination_allowed(NULL));
 puts("embedded compression source/destination path limits: PASS");
 return 0;
}
