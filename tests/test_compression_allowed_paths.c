#define _DEFAULT_SOURCE
#include "../src/native/compression_allowed_paths.h"
#include <stdlib.h>
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
 /* Anchored filesystem admission is separate from text allowlists.
  * All mount/data prefixes must resolve through real directories; even a
  * harmless-looking source path with a symlink ancestor is rejected. */
 char sandbox[]="/tmp/cmp5-game-paths-XXXXXX";
 assert(mkdtemp(sandbox));
 char folder[512],image[512],external[512],alias[512];
 const char*dirs[]={
   "/data","/data/homebrew","/data/etaHEN","/data/etaHEN/games",
   "/mnt","/mnt/usb0","/mnt/usb0/homebrew",
   "/mnt/ext7","/mnt/ext7/homebrew"};
 for(size_t i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++){
  int n=snprintf(folder,sizeof(folder),"%s%s",sandbox,dirs[i]);
  assert(n>0&&(size_t)n<sizeof(folder));
  assert(mkdir(folder,0700)==0);
 }
 assert(snprintf(folder,sizeof(folder),"%s/data/homebrew/PPSA12345",
                 sandbox)>0);
 assert(mkdir(folder,0700)==0);
 assert(snprintf(image,sizeof(image),"%s/mnt/usb0/GAME.exfat",sandbox)>0);
 FILE*out=fopen(image,"wb");assert(out);
 assert(fputs("SYNTHETIC_GAME_IMAGE",out)>=0&&fclose(out)==0);
 assert(cmp5_game_path_on_disk(sandbox,"/data/homebrew/PPSA12345",false));
 assert(cmp5_game_path_on_disk(sandbox,"/mnt/usb0/GAME.exfat",false));
 assert(cmp5_game_path_on_disk(sandbox,"/mnt/usb0/homebrew",true));
 /* Refuse existing managed title output, including dangling symlinks. */
 assert(cmp5_game_output_vacant(sandbox,"/mnt/usb0/homebrew","PPSA12345"));
 assert(!cmp5_game_output_vacant(sandbox,"/mnt/usb0/homebrew","PPSA1234A"));
 assert(!cmp5_game_output_vacant(sandbox,"/mnt/usb0/homebrew",".."));
 char reserved[512];
 assert(snprintf(reserved,sizeof(reserved),"%s/mnt/usb0/homebrew/PPSA12345",sandbox)>0);
 assert(mkdir(reserved,0700)==0);
 assert(!cmp5_game_output_vacant(sandbox,"/mnt/usb0/homebrew","PPSA12345"));
 assert(rmdir(reserved)==0);
 assert(symlink("/system_data",reserved)==0);
 assert(!cmp5_game_output_vacant(sandbox,"/mnt/usb0/homebrew","PPSA12345"));
 assert(unlink(reserved)==0);
 assert(cmp5_game_output_vacant(sandbox,"/mnt/usb0/homebrew","PPSA12345"));
 assert(!cmp5_game_path_on_disk(sandbox,"/mnt/usb0/GAME.exfat",true));
 assert(!cmp5_game_path_on_disk(sandbox,"/mnt/usb0/GAME.exfat/missing",false));
 assert(!cmp5_game_path_on_disk(sandbox,"/mnt/usb0/ABSENT.exfat",false));
 assert(snprintf(external,sizeof(external),"%s/protected",sandbox)>0);
 assert(mkdir(external,0700)==0);
 assert(snprintf(alias,sizeof(alias),"%s/mnt/usb0/homebrew/DECOY",sandbox)>0);
 assert(symlink(external,alias)==0);
 assert(!cmp5_game_path_on_disk(sandbox,"/mnt/usb0/homebrew/DECOY",false));
 assert(unlink(alias)==0);
 assert(snprintf(alias,sizeof(alias),"%s/mnt/usb0/homebrew",sandbox)>0);
 assert(rmdir(alias)==0);
 assert(symlink(external,alias)==0);
 assert(!cmp5_game_path_on_disk(sandbox,"/mnt/usb0/homebrew",true));
 assert(!cmp5_game_path_on_disk(sandbox,"/mnt/usb0/homebrew/DECOY",false));
 assert(unlink(alias)==0);
 assert(mkdir(alias,0700)==0);
 assert(snprintf(alias,sizeof(alias),"%s/mnt/usb0/GAME.link",sandbox)>0);
 assert(link(image,alias)==0);
 assert(!cmp5_game_path_on_disk(sandbox,"/mnt/usb0/GAME.exfat",false));
 assert(unlink(alias)==0);
 /* The name may stay syntactically valid while a different real file
  * replaces it; a second allowlist check alone would accept that file. */
 struct stat game_before={0},dest_before={0};
 assert(cmp5_game_path_stat(sandbox,"/mnt/usb0/GAME.exfat",false,&game_before));
 assert(cmp5_game_path_stat(sandbox,"/mnt/usb0/homebrew",true,&dest_before));
 assert(cmp5_game_path_still_same(sandbox,"/mnt/usb0/GAME.exfat",false,&game_before));
 assert(cmp5_game_path_still_same(sandbox,"/mnt/usb0/homebrew",true,&dest_before));
 char old_image[512];
 assert(snprintf(old_image,sizeof(old_image),"%s/mnt/usb0/GAME.old",sandbox)>0);
 assert(rename(image,old_image)==0);
 FILE*replacement=fopen(image,"wb");assert(replacement);
 assert(fputs("SYNTHETIC_GAME_IMAGE",replacement)>=0);
 assert(fclose(replacement)==0);
 assert(cmp5_game_path_on_disk(sandbox,"/mnt/usb0/GAME.exfat",false));
 assert(!cmp5_game_path_still_same(sandbox,"/mnt/usb0/GAME.exfat",false,&game_before));
 assert(unlink(old_image)==0);
 /* A directory of the same permitted name but different inode is
  * not the destination the user approved. */
 char old_dest[512];
 assert(snprintf(old_dest,sizeof(old_dest),"%s/mnt/usb0/homebrew_old",sandbox)>0);
 assert(snprintf(alias,sizeof(alias),"%s/mnt/usb0/homebrew",sandbox)>0);
 assert(rename(alias,old_dest)==0);
 assert(mkdir(alias,0700)==0);
 assert(cmp5_game_path_on_disk(sandbox,"/mnt/usb0/homebrew",true));
 assert(!cmp5_game_path_still_same(sandbox,"/mnt/usb0/homebrew",true,&dest_before));
 assert(rmdir(old_dest)==0);
 assert(unlink(image)==0);
 assert(rmdir(folder)==0);
 /* All other directories are removed by the reverse-order fixture loop. */
 for(size_t i=sizeof(dirs)/sizeof(dirs[0]);i>0;i--){
  int n=snprintf(folder,sizeof(folder),"%s%s",sandbox,dirs[i-1]);
  assert(n>0&&(size_t)n<sizeof(folder));
  assert(rmdir(folder)==0);
 }
 assert(rmdir(external)==0);
 assert(rmdir(sandbox)==0);
 assert(!cmp5_game_source_allowed(NULL));
 assert(!cmp5_game_destination_allowed(NULL));
 puts("embedded compression source/destination path limits: PASS");
 return 0;
}
