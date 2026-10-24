/* Host-only fault injection of real game scanner, no PS5 access. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int fault_mode=0,fault_hits=0;
static int injected_fstatat(int fd,const char*name,struct stat*st,int flags){
 if(fault_mode==1&&!strcmp(name,"FAULT_ENTRY.txt")){
  ++fault_hits;errno=EIO;return -1;
 }
 return fstatat(fd,name,st,flags);
}
static int injected_openat(int fd,const char*name,int flags,...){
 if(fault_mode==2&&!strcmp(name,"FAULT_GAME")){
  ++fault_hits;errno=EACCES;return -1;
 }
 return openat(fd,name,flags);
}
#define fstatat injected_fstatat
#define openat injected_openat
#include "../src/native/game_images.c"
#undef fstatat
#undef openat
int main(int argc,char**argv){
 if(argc!=3)return 2;
 fault_mode=!strcmp(argv[2],"stat")?1:
            !strcmp(argv[2],"dir")?2:
            !strcmp(argv[2],"normal")?0:-1;
 assert(fault_mode>=0);
 Cmp5GameImageStats result={0};
 assert(cmp5_scan_game_images(argv[1],&result,NULL,NULL));
 assert(result.scanned_roots>=1u);
 assert(result.count[CMP5_IMAGE_EXFAT]==1u);
 assert(result.shown_images==1u);
 assert(!strcmp(result.image[0].label,"HEALTHY.exfat"));
 if(!fault_mode){
  assert(fault_hits==0);
  assert(!result.partial&&result.folder_games==1u);
 }else{
  assert(fault_hits==1);
  assert(result.partial);
  assert(result.folder_games==(fault_mode==1?1u:0u));
 }
 puts("Game metadata I/O faults: PASS (healthy candidates retained)");
 return 0;
}
