/* Deterministically deny a cross-tree thumbnail without needing ACLs.
 * Compile the real scanner source once, wrapping just openat. */
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
static int rejects=0,deny_dat=0,denied_dat_calls=0;
static int injected_fstatat(int dir,const char*name,struct stat*st,int flags){
 if(deny_dat&&strcmp(name,"REC_001.dat")==0){
  ++denied_dat_calls;
  errno=deny_dat==1?EACCES:EPERM;
  return -1;
 }
 return fstatat(dir,name,st,flags);
}
static int injected_openat(int fd,const char*name,int flags,...){
 if(strcmp(name,"REC_001.jpg.jpeg")==0){
  ++rejects;errno=EACCES;return -1;
 }
 return openat(fd,name,flags);
}
#define openat injected_openat
#define fstatat injected_fstatat
#include "../src/native/capture_companions.c"
#undef fstatat
#undef openat
int main(int argc,char**argv){
 if(argc!=4)return 2;
 const char*root=argv[1],*media=argv[2],*relative=argv[3];
 struct stat st={0};
 assert(stat(media,&st)==0);
 Cmp5MediaCandidate photo={0};
 assert(strlen(relative)<sizeof(photo.preview_path));
 strcpy(photo.preview_path,relative);
 photo.media_type=1u;
 photo.bytes=(uint64_t)st.st_size;
 cmp5_snapshot_capture(&photo,&st);
 Cmp5CompanionReport observed={0};
 assert(!cmp5_inspect_capture_companions(root,&photo,&observed,NULL,NULL));
 assert(rejects>0&&observed.state==CMP5_COMPANIONS_UNVERIFIABLE);
 assert(observed.failure==CMP5_COMPANION_FAILURE_THUMBNAIL_UNAVAILABLE);
 assert(observed.thumbnail==CMP5_THUMBNAIL_INACCESSIBLE);
 assert(observed.files==0u&&observed.media==0u&&
        observed.sidecars==0u&&observed.nominal_bytes==0u);
 Cmp5MediaCandidate plan[CMP5_GROUP_MAX];
 memset(plan,0x5Au,sizeof(plan));
 uint32_t count=42u;
 assert(!cmp5_plan_capture_group(root,&photo,plan,&count,NULL,NULL));
 assert(count==0u);
 const unsigned char*bytes=(const unsigned char*)plan;
 for(size_t i=0;i<sizeof(plan);i++)assert(bytes[i]==0u);
 /* Companion metadata denied during enumeration: this is a permission
  * problem, not evidence that a suspicious variant exists or is absent.
  * Neither native counters nor private backup group approval may proceed. */
 for(int mode=1;mode<=2;mode++){
  deny_dat=mode;denied_dat_calls=0;rejects=0;
  Cmp5CompanionReport denied={0};
  assert(!cmp5_inspect_capture_companions(root,&photo,&denied,NULL,NULL));
  assert(denied_dat_calls>0&&rejects==0);
  assert(denied.state==CMP5_COMPANIONS_UNVERIFIABLE);
  assert(denied.failure==CMP5_COMPANION_FAILURE_PERMISSION_DENIED);
  assert(denied.files==0u&&denied.media==0u&&
         denied.sidecars==0u&&denied.nominal_bytes==0u);
  Cmp5MediaCandidate unknown[CMP5_GROUP_MAX];
  memset(unknown,0xA5u,sizeof(unknown));
  uint32_t qty=99u;
  assert(!cmp5_plan_capture_group(root,&photo,unknown,&qty,NULL,NULL));
  assert(qty==0u);
  const unsigned char*raw=(const unsigned char*)unknown;
  for(size_t i=0;i<sizeof(unknown);i++)assert(raw[i]==0u);
 }
 puts("Thumbnail and same-stem DAT EACCES/EPERM: fail closed: PASS");
 return 0;
}
