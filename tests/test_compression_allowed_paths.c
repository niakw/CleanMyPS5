#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
/* Swap the authorized destination pathname immediately after creating
 * its title directory, while the engine still holds the old parent fd. */
static char moved_output_from[512],moved_output_to[512];
static int move_output_after_create=0;
static char moved_title_from[512],moved_title_to[512];
static int move_title_after_stat=0;
static int expose_checkpoint_mode_once=0;
static int injected_fstatat(int fd,const char*name,struct stat*out,int flags){
 const int result=fstatat(fd,name,out,flags);
 if(result==0&&expose_checkpoint_mode_once&&
    !strcmp(name,".e2a-state")){
  /* Simulate a filesystem which ignores mode 0700. */
  out->st_mode|=0077;
  expose_checkpoint_mode_once=0;
 }
 if(result==0&&move_title_after_stat&&!strcmp(name,"PPSA88888")){
  assert(rename(moved_title_from,moved_title_to)==0);
  assert(mkdir(moved_title_from,0755)==0);
  move_title_after_stat=0;
 }
 return result;
}
static int injected_mkdirat(int dir,const char*name,mode_t mode){
 const int result=mkdirat(dir,name,mode);
 if(result==0&&move_output_after_create&&!strcmp(name,"PPSA77777")){
  assert(rename(moved_output_from,moved_output_to)==0);
  assert(mkdir(moved_output_from,0700)==0);
  move_output_after_create=0;
 }
 return result;
}
#define mkdirat injected_mkdirat
#define fstatat injected_fstatat
#include "../src/native/compression_allowed_paths.h"
#undef fstatat
#undef mkdirat
#include "../src/native/ampr_checkpoint_request.h"
int main(void) {
 /* The title's READ-ONLY checkpoint path prefilter must agree with
  * the bundled packer allowlist, without linking packer write helpers. */
 static const char*const candidates[]={
  "/data/homebrew/PPSA12345",
  "/data/homebrew/Game..USA",
  "/data/etaHEN/games/Game [US]",
  "/data/homebrew/GAME.exfat",
  "/mnt/usb0/homebrew/PPSA12345",
  "/mnt/ext7/etaHEN/games/A Game [US]",
  "/mnt/usb0/GAME.exfat",
  "/mnt/usb0/GAME.FFPKG",
  "/mnt/ext7/DUMP.ffpfs",
  "/mnt/usb0/DUMP.ffpfsc",
  "/mnt/usb0/homebrew/GAME.exfat",
  "/mnt/usb0/games/PPSA12345",
  "/mnt/usb0/savedata/foo",
  "/mnt/usb0/.hidden.exfat",
  "/mnt/usb0/GAME.iso",
  "/data/homebrew/.hidden",
  "/data/homebrew/../PPSA12345",
  "/data/homebrew/GAME/foo",
  "/mnt/ext8/GAME.exfat",
  "/mnt/usbx/GAME.exfat",
  "/mnt/usb0/homebrew/APP;rm",
  "/mnt/usb0/homebrew/PPSA12345/../",
  "/data/homebrew/",
  "/mnt/usb0/",
  "/mnt/usb0"
 };
 for(size_t i=0;i<sizeof(candidates)/sizeof(candidates[0]);i++)
  assert(cmp5_ampr_source_candidate(candidates[i])==
         cmp5_game_source_allowed(candidates[i]));

 /* A saved checkpoint with old destructive settings is NOT a native
  * CleanMyPS5 job. Do not silently normalize/adopt its request.bin.
  * A legitimate saved request requires verify+dedup and no mutation
  * of source, other outputs, caches or test hooks. */
 assert(cmp5_game_safe_checkpoint_options(0,0,0,0,1,1,0,0));
 assert(!cmp5_game_safe_checkpoint_options(1,0,0,0,1,1,0,0));
 assert(!cmp5_game_safe_checkpoint_options(0,1,0,0,1,1,0,0));
 assert(!cmp5_game_safe_checkpoint_options(0,0,1,0,1,1,0,0));
 assert(!cmp5_game_safe_checkpoint_options(0,0,0,1,1,1,0,0));
 assert(!cmp5_game_safe_checkpoint_options(0,0,0,0,0,1,0,0));
 assert(!cmp5_game_safe_checkpoint_options(0,0,0,0,1,0,0,0));
 assert(!cmp5_game_safe_checkpoint_options(0,0,0,0,1,1,1,0));
 assert(!cmp5_game_safe_checkpoint_options(0,0,0,0,1,1,0,1));
 assert(!cmp5_game_safe_checkpoint_options(0,0,0,0,1,1,0,UINT64_MAX));
 assert(!cmp5_game_safe_checkpoint_options(0,0,0,0,2,1,0,0));
 assert(!cmp5_game_safe_checkpoint_options(0,0,0,0,1,2,0,0));
 /* Truncated/unterminated SMP fields must fail closed. */
 char unterminated[16];memset(unterminated,'Z',sizeof(unterminated));
 assert(!cmp5_game_smp_field_eq(unterminated,sizeof(unterminated),"ZZZ"));
 assert(!cmp5_game_smp_field_eq(unterminated,0,"Z"));
 assert(!cmp5_game_smp_field_eq(NULL,16,"Z"));
 assert(!cmp5_game_smp_field_eq(unterminated,16,NULL));
 const char source_type[16]="image";
 /* The expected value is bounded too: a truncated resume field without
  * a terminator must not make the library scan past its declared buffer. */
 assert(!cmp5_game_smp_field_eq(source_type,sizeof(source_type),
                                unterminated));
 assert(cmp5_game_smp_field_eq(source_type,sizeof(source_type),"image"));
 assert(!cmp5_game_smp_field_eq(source_type,sizeof(source_type),"images"));
 /* The expected literal is shorter than the SMP fixed-width field. */
 char wide_source_type[128]={0};
 memcpy(wide_source_type,"image",6);
 assert(cmp5_game_smp_field_eq(wide_source_type,sizeof(wide_source_type),"image"));
 assert(!cmp5_game_smp_field_eq(wide_source_type,sizeof(wide_source_type),"images"));
 char expected_unterminated[128];
 memset(expected_unterminated,'i',sizeof(expected_unterminated));
 assert(!cmp5_game_smp_field_eq(wide_source_type,sizeof(wide_source_type),
                                expected_unterminated));
 const char image_type[16]="exfatfs";
 assert(cmp5_game_smp_field_eq(image_type,sizeof(image_type),"exfatfs"));
 assert(!cmp5_game_smp_field_eq(source_type,3,"image"));
 char tail[16]={'i','m','a','g','e','\0','B','A','D'};
 assert(cmp5_game_smp_field_eq(tail,sizeof(tail),"image"));
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
 /* The lexical allowlist admits direct entries in homebrew, but regular
  * README/config files MUST be rejected before any SMP or packer probe. */
 assert(cmp5_game_source_allowed("/data/homebrew/README.txt"));
 char ordinary[512];
 assert(snprintf(ordinary,sizeof(ordinary),"%s/data/homebrew/README.txt",
                 sandbox)>0);
 FILE*note=fopen(ordinary,"wb");assert(note);
 assert(fputs("NOT A GAME",note)>=0&&fclose(note)==0);
 assert(!cmp5_game_path_on_disk(sandbox,"/data/homebrew/README.txt",false));
 assert(!cmp5_convertible_image_suffix("/data/homebrew/README.txt"));
 assert(cmp5_convertible_image_suffix("/data/homebrew/GAME.ffpkg"));
 assert(cmp5_convertible_image_suffix("/data/homebrew/GAME.FFPFS"));
 assert(cmp5_image_suffix("/data/homebrew/GAME.ffpfsc"));
 assert(!cmp5_convertible_image_suffix("/data/homebrew/GAME.ffpfsc"));
 assert(!cmp5_image_suffix(NULL));
 assert(unlink(ordinary)==0);
 /* A regular .ffpfsc file is already compressed and may be inventoried,
  * but must never be accepted as a fresh AMPR source. */
 assert(snprintf(ordinary,sizeof(ordinary),"%s/data/homebrew/DONE.ffpfsc",
                 sandbox)>0);
 note=fopen(ordinary,"wb");assert(note);
 assert(fputs("ALREADY COMPRESSED",note)>=0&&fclose(note)==0);
 assert(!cmp5_game_path_on_disk(sandbox,"/data/homebrew/DONE.ffpfsc",
                                false));
 assert(unlink(ordinary)==0);
 /* Image extension alone is not an image file. The engine must use the
  * exact file type from its descriptor-anchored admission snapshot. */
 struct stat image_file={0},game_folder={0};
 assert(cmp5_game_path_stat(sandbox,"/mnt/usb0/GAME.exfat",false,&image_file));
 assert(cmp5_source_requires_smp("/mnt/usb0/GAME.exfat",&image_file));
 assert(cmp5_source_requires_smp("/mnt/usb0/GAME.EXFAT",&image_file));
 assert(cmp5_source_requires_smp("/mnt/usb0/GAME.ffpfsc",&image_file));
 assert(!cmp5_source_requires_smp("/mnt/usb0/GAME.exfat",NULL));
 assert(!cmp5_source_requires_smp(NULL,&image_file));
 assert(cmp5_game_path_stat(sandbox,"/data/homebrew/PPSA12345",false,&game_folder));
 assert(!cmp5_source_requires_smp("/data/homebrew/PPSA12345",&game_folder));
 char extracted[512];
 assert(snprintf(extracted,sizeof(extracted),
                 "%s/mnt/usb0/homebrew/EXTRACTED.exfat",sandbox)>0);
 assert(mkdir(extracted,0700)==0);
 assert(cmp5_game_path_stat(sandbox,"/mnt/usb0/homebrew/EXTRACTED.exfat",
                            false,&game_folder));
 assert(!cmp5_source_requires_smp("/mnt/usb0/homebrew/EXTRACTED.exfat",
                                   &game_folder));
 /* A genuinely extracted game can have an image-like DIRECTORY name;
  * extension alone must not turn it into an SMP image. */
 assert(cmp5_game_path_stat(sandbox,
            "/mnt/usb0/homebrew/EXTRACTED.exfat",false,&game_folder));
 assert(rmdir(extracted)==0);
 assert(cmp5_game_path_on_disk(sandbox,"/mnt/usb0/homebrew",true));
 /* Image lookup must never inspect unauthorized console directories,
  * and direct preflight must use a proven writable target root. */
 struct stat preflight_source={0},preflight_dest={0};
 assert(!cmp5_game_source_allowed("/user/savedata/PRIVATE.exfat"));
 assert(!cmp5_game_path_stat(sandbox,"/user/savedata/PRIVATE.exfat",
                             false,&preflight_source));
 assert(!cmp5_game_source_allowed("/system_data/priv/GAME.ffpkg"));
 assert(cmp5_game_path_stat(sandbox,"/mnt/usb0/GAME.exfat",
                            false,&preflight_source));
 assert(cmp5_game_path_stat(sandbox,"/mnt/usb0/homebrew",
                            true,&preflight_dest));
 assert(cmp5_source_requires_smp("/mnt/usb0/GAME.exfat",
                                 &preflight_source));
 assert(!cmp5_game_destination_allowed("/user/savedata"));
 /* Refuse existing managed title output, including dangling symlinks. */
 assert(cmp5_game_output_vacant(sandbox,"/mnt/usb0/homebrew","PPSA12345"));
 struct stat created={0};
 /* Even under a maximally permissive process umask, game output must
  * not be world writable and the checkpoint must be private. */
 mode_t saved_umask=umask(0);
 const bool created_ok=cmp5_game_output_create_unique(
   sandbox,"/mnt/usb0/homebrew","PPSA55555",&created);
 (void)umask(saved_umask);
 assert(created_ok);
 assert(S_ISDIR(created.st_mode));
 assert((created.st_mode&0777)==0755);
 assert(cmp5_game_resume_path_still_same(sandbox,
        "/mnt/usb0/homebrew/PPSA55555",&created));
 assert(!cmp5_game_output_create_unique(sandbox,"/mnt/usb0/homebrew",
                                        "PPSA55555",&created));
 assert(!cmp5_game_output_create_unique(sandbox,"/mnt/usb0/homebrew",
                                        "../WRONG",&created));
 assert(!cmp5_game_output_create_unique(sandbox,"/user/savedata",
                                        "PPSA66666",&created));
 char created_path[512];
 assert(snprintf(created_path,sizeof(created_path),
                 "%s/mnt/usb0/homebrew/PPSA55555/.e2a-state",sandbox)>0);
 struct stat checkpoint={0};
 assert(stat(created_path,&checkpoint)==0&&S_ISDIR(checkpoint.st_mode));
 assert((checkpoint.st_mode&0777)==0700);
 int state_handle=cmp5_game_open_checkpoint_dir(
   sandbox,"/mnt/usb0/homebrew/PPSA55555/.e2a-state");
 assert(state_handle>=0);
 assert(close(state_handle)==0);
 /* A restrictive process umask can silently turn 0755 into 0700.
  * Refuse to report a launchable title; leave its partial output intact
  * for a manual audit rather than silently chmod/unlink another inode. */
 mode_t restrictive_saved=umask(0077);
 struct stat restricted_output={0};
 const bool restrictive_created=cmp5_game_output_create_unique(
   sandbox,"/mnt/usb0/homebrew","PPSA66666",&restricted_output);
 (void)umask(restrictive_saved);
 assert(!restrictive_created&&restricted_output.st_ino==0);
 char restricted_title[512],restricted_state[512];
 assert(snprintf(restricted_title,sizeof(restricted_title),
   "%s/mnt/usb0/homebrew/PPSA66666",sandbox)>0);
 assert(snprintf(restricted_state,sizeof(restricted_state),
   "%s/.e2a-state",restricted_title)>0);
 struct stat mode_observed={0};
 assert(stat(restricted_title,&mode_observed)==0&&
        (mode_observed.st_mode&0777)==0700);
 assert(stat(restricted_state,&mode_observed)==0&&
        (mode_observed.st_mode&0777)==0700);
 assert(rmdir(restricted_state)==0);
 assert(rmdir(restricted_title)==0);
 assert(cmp5_game_open_checkpoint_dir(
   sandbox,"/mnt/usb0/homebrew/PPSA55555/../.e2a-state")<0);
 assert(cmp5_game_open_checkpoint_dir(
   sandbox,"/user/av_contents/PPSA55555/.e2a-state")<0);
 assert(rmdir(created_path)==0);
 /* Existing output with a symbolic checkpoint must NEVER be resumed. */
 assert(symlink("/system_data",created_path)==0);
 assert(cmp5_game_open_checkpoint_dir(
   sandbox,"/mnt/usb0/homebrew/PPSA55555/.e2a-state")<0);
 assert(unlink(created_path)==0);
 assert(snprintf(created_path,sizeof(created_path),
                 "%s/mnt/usb0/homebrew/PPSA55555",sandbox)>0);
 assert(rmdir(created_path)==0);
 assert(!cmp5_game_output_vacant(sandbox,"/mnt/usb0/homebrew","PPSA1234A"));
 assert(!cmp5_game_output_vacant(sandbox,"/mnt/usb0/homebrew",".."));
 char reserved[512];
 assert(snprintf(reserved,sizeof(reserved),"%s/mnt/usb0/homebrew/PPSA12345",sandbox)>0);
 assert(mkdir(reserved,0700)==0);
 assert(!cmp5_game_output_vacant(sandbox,"/mnt/usb0/homebrew","PPSA12345"));
 struct stat resume_snapshot={0};
 assert(cmp5_game_resume_path_stat(sandbox,
   "/mnt/usb0/homebrew/PPSA12345",&resume_snapshot));
 assert(cmp5_game_resume_path_still_same(sandbox,
   "/mnt/usb0/homebrew/PPSA12345",&resume_snapshot));
 assert(!cmp5_game_resume_path_stat(sandbox,
   "/mnt/usb0/homebrew/PPSA1234A",&resume_snapshot));
 assert(!cmp5_game_resume_path_stat(sandbox,
   "/mnt/usb0/savedata/PPSA12345",&resume_snapshot));
 assert(!cmp5_game_resume_path_stat(sandbox,
   "/mnt/usb0/homebrew/../PPSA12345",&resume_snapshot));
 char moved_title[512];
 assert(snprintf(moved_title,sizeof(moved_title),
   "%s/mnt/usb0/homebrew/PPSA12345.moved",sandbox)>0);
 assert(rename(reserved,moved_title)==0);
 assert(mkdir(reserved,0700)==0);
 assert(!cmp5_game_resume_path_still_same(sandbox,
   "/mnt/usb0/homebrew/PPSA12345",&resume_snapshot));
 assert(rmdir(reserved)==0);
 assert(rename(moved_title,reserved)==0);
 assert(rmdir(reserved)==0);
 assert(symlink("/system_data",reserved)==0);
 assert(!cmp5_game_output_vacant(sandbox,"/mnt/usb0/homebrew","PPSA12345"));
 assert(!cmp5_game_output_create_unique(sandbox,"/mnt/usb0/homebrew",
                                        "PPSA12345",&created));
 assert(!cmp5_game_resume_path_stat(sandbox,
   "/mnt/usb0/homebrew/PPSA12345",&resume_snapshot));
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
 assert(snprintf(moved_output_from,sizeof(moved_output_from),
   "%s/mnt/usb0/homebrew",sandbox)>0);
 assert(snprintf(moved_output_to,sizeof(moved_output_to),
   "%s/mnt/usb0/homebrew_swapped",sandbox)>0);
 move_output_after_create=1;
 struct stat rejected_output={0};
 assert(!cmp5_game_output_create_unique(sandbox,"/mnt/usb0/homebrew",
                                        "PPSA77777",&rejected_output));
 assert(!move_output_after_create);
 assert(rejected_output.st_ino==0);
 assert(!cmp5_game_path_on_disk(sandbox,
   "/mnt/usb0/homebrew/PPSA77777",false));
 char displaced_checkpoint[512],displaced_title[512];
 assert(snprintf(displaced_title,sizeof(displaced_title),
   "%s/PPSA77777",moved_output_to)>0);
 assert(snprintf(displaced_checkpoint,sizeof(displaced_checkpoint),
   "%s/.e2a-state",displaced_title)>0);
 struct stat displaced_stat={0};
 assert(stat(displaced_checkpoint,&displaced_stat)==0&&
        S_ISDIR(displaced_stat.st_mode));
 /* Partial output remains for audit: only the synthetic test deletes it. */
 assert(rmdir(displaced_checkpoint)==0);
 assert(rmdir(displaced_title)==0);
 assert(rmdir(moved_output_to)==0);
 /* The parent can remain unchanged while the output TITLE itself is
  * substituted after the first inode comparison. This must also fail. */
 assert(snprintf(moved_title_from,sizeof(moved_title_from),
   "%s/PPSA88888",moved_output_from)>0);
 assert(snprintf(moved_title_to,sizeof(moved_title_to),
   "%s/PPSA88888_moved",moved_output_from)>0);
 move_title_after_stat=1;
 struct stat rejected_title={0};
 assert(!cmp5_game_output_create_unique(sandbox,"/mnt/usb0/homebrew",
                                        "PPSA88888",&rejected_title));
 assert(!move_title_after_stat);
 assert(rejected_title.st_ino==0);
 assert(stat(moved_title_from,&displaced_stat)==0&&
        S_ISDIR(displaced_stat.st_mode));
 assert(snprintf(displaced_checkpoint,sizeof(displaced_checkpoint),
   "%s/.e2a-state",moved_title_to)>0);
 assert(stat(displaced_checkpoint,&displaced_stat)==0&&
        S_ISDIR(displaced_stat.st_mode));
 assert(rmdir(displaced_checkpoint)==0);
 assert(rmdir(moved_title_to)==0);
 assert(rmdir(moved_title_from)==0);
 /* The storage driver can ignore restrictive mkdir permissions.
  * The app must decline a publicly readable private checkpoint. */
 expose_checkpoint_mode_once=1;
 struct stat unsafe_output={0};
 assert(!cmp5_game_output_create_unique(sandbox,"/mnt/usb0/homebrew",
                                        "PPSA99999",&unsafe_output));
 assert(!expose_checkpoint_mode_once);
 assert(unsafe_output.st_ino==0);
 assert(snprintf(displaced_title,sizeof(displaced_title),
   "%s/PPSA99999",moved_output_from)>0);
 assert(snprintf(displaced_checkpoint,sizeof(displaced_checkpoint),
   "%s/.e2a-state",displaced_title)>0);
 assert(stat(displaced_checkpoint,&displaced_stat)==0);
 assert(rmdir(displaced_checkpoint)==0);
 assert(rmdir(displaced_title)==0);
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
