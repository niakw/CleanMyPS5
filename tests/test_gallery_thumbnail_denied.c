/* Deterministically deny a cross-tree thumbnail without needing ACLs.
 * Compile the real scanner source once, wrapping just openat. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int rejects=0,deny_dat=0,denied_dat_calls=0;
static int reject_thumbnail=1,change_namespace=0,namespace_changes=0;
static dev_t thumbnail_dev=0;
static ino_t thumbnail_ino=0;
/* Host-only: modify the thumbnail directory during its readdir walk. */
static struct dirent* injected_readdir(DIR*stream){
 if(change_namespace&&!namespace_changes){
  const int descriptor=dirfd(stream);
  struct stat st={0};
  if(descriptor>=0&&fstat(descriptor,&st)==0&&
     st.st_dev==thumbnail_dev&&st.st_ino==thumbnail_ino){
   const mode_t changed=(st.st_mode&07777)^S_IXGRP;
   assert(fchmod(descriptor,changed)==0);
   namespace_changes++;
  }
 }
 return readdir(stream);
}
static int injected_fstatat(int dir,const char*name,struct stat*st,int flags){
 if(deny_dat&&strcmp(name,"REC_001.dat")==0){
  ++denied_dat_calls;
  errno=deny_dat==1?EACCES:EPERM;
  return -1;
 }
 return fstatat(dir,name,st,flags);
}
static int injected_openat(int fd,const char*name,int flags,...){
 if(reject_thumbnail&&strcmp(name,"REC_001.jpg.jpeg")==0){
  ++rejects;errno=EACCES;return -1;
 }
 return openat(fd,name,flags);
}
#define openat injected_openat
#define fstatat injected_fstatat
#define readdir injected_readdir
#include "../src/native/capture_companions.c"
#undef readdir
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
 deny_dat=0;
 reject_thumbnail=0;
 char thumbnail_dir[1024];
 const int written=snprintf(thumbnail_dir,sizeof(thumbnail_dir),
   "%s/user/av_contents/thumbnails/photo/NPXS00000/GAME123",root);
 assert(written>0&&(size_t)written<sizeof(thumbnail_dir));
 struct stat thumb_stat={0};
 assert(stat(thumbnail_dir,&thumb_stat)==0);
 thumbnail_dev=thumb_stat.st_dev;
 thumbnail_ino=thumb_stat.st_ino;
 Cmp5CompanionReport stable={0};
 assert(cmp5_inspect_capture_companions(root,&photo,&stable,NULL,NULL));
 assert(stable.thumbnail==CMP5_THUMBNAIL_CANDIDATE_FOUND);
 change_namespace=1;
 namespace_changes=0;
 Cmp5CompanionReport raced={0};
 assert(!cmp5_inspect_capture_companions(root,&photo,&raced,NULL,NULL));
 assert(namespace_changes==1);
 assert(raced.state==CMP5_COMPANIONS_UNVERIFIABLE);
 assert(raced.thumbnail==CMP5_THUMBNAIL_UNSAFE);
 assert(raced.failure==CMP5_COMPANION_FAILURE_THUMBNAIL_UNSAFE);
 assert(raced.files==0u&&raced.nominal_bytes==0u);
 /* The independent private backup planner must reject the same race. */
 namespace_changes=0;
 Cmp5MediaCandidate raced_group[CMP5_GROUP_MAX];
 memset(raced_group,0xA5,sizeof(raced_group));
 uint32_t raced_count=77u;
 assert(!cmp5_plan_capture_group(root,&photo,raced_group,
                                 &raced_count,NULL,NULL));
 assert(namespace_changes==1&&raced_count==0u);
 const unsigned char*zeroed=(const unsigned char*)raced_group;
 for(size_t i=0;i<sizeof(raced_group);i++)assert(zeroed[i]==0u);
 puts("Thumbnail DAT permissions and enumeration race: blocked: PASS");
 return 0;
}
