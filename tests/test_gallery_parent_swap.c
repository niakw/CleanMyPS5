/* A Gallery parent can be moved after inspection from its anchored
 * descriptor, then replaced at the selected pathname. Never advertise
 * old album members or thumbnails as current. Host fixture only. */
#define _DEFAULT_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
static char old_parent[1024],moved_parent[1024];
static int mode,swapped,thumbnail_named;
static void swap_parent(void){
 assert(rename(old_parent,moved_parent)==0);
 assert(mkdir(old_parent,0700)==0);
 swapped=1;
}
static int intercepted_closedir(DIR*stream){
 const int rc=closedir(stream);
 if(rc==0&&mode==1&&!swapped)swap_parent();
 return rc;
}
static int intercepted_fstatat(int dir,const char*name,struct stat*info,int flags){
 const int rc=fstatat(dir,name,info,flags);
 if(mode==2&&rc==0&&!strcmp(name,"REC_001.jpg.jpeg"))
  thumbnail_named=1;
 return rc;
}
static int intercepted_fstat(int fd,struct stat*info){
 const int rc=fstat(fd,info);
 if(rc==0&&mode==2&&thumbnail_named&&!swapped&&S_ISDIR(info->st_mode)){
  struct stat path_info={0};
  if(stat(old_parent,&path_info)==0&&
     info->st_dev==path_info.st_dev&&info->st_ino==path_info.st_ino)
   swap_parent(); /* fstat returned old metadata; path changes NOW */
 }
 return rc;
}
#define closedir intercepted_closedir
#define fstatat intercepted_fstatat
#define fstat intercepted_fstat
#include "../src/native/capture_companions.c"
#undef fstat
#undef fstatat
#undef closedir
int main(int argc,char**argv){
 /* A primary *.jpg can fit the final byte of preview_path while its
  * same-stem *.jpeg companion would overflow by exactly one byte. */
 assert(companion_locator_fits(249u,5u,256u));
 assert(!companion_locator_fits(249u,6u,256u));
 assert(!companion_locator_fits(250u,5u,256u));
 assert(!companion_locator_fits(SIZE_MAX,1u,256u));
 assert(!companion_locator_fits(0u,0u,0u));
 if(argc!=7)return 2;
 const char*root=argv[1],*media=argv[2],*relative=argv[3];
 if(snprintf(old_parent,sizeof(old_parent),"%s",argv[4])<=0||
    snprintf(moved_parent,sizeof(moved_parent),"%s",argv[5])<=0)return 3;
 mode=!strcmp(argv[6],"media")?1:!strcmp(argv[6],"thumbnail")?2:0;
 if(!mode)return 3;
 Cmp5MediaCandidate selected={0};
 struct stat st={0};
 assert(stat(media,&st)==0);
 assert(strlen(relative)<sizeof(selected.preview_path));
 strcpy(selected.preview_path,relative);
 selected.media_type=1u;
 selected.bytes=(uint64_t)st.st_size;
 cmp5_snapshot_capture(&selected,&st);
 Cmp5CompanionReport report={0};
 assert(!cmp5_inspect_capture_companions(root,&selected,&report,NULL,NULL));
 assert(swapped);
 assert(report.state==CMP5_COMPANIONS_UNVERIFIABLE);
 assert(report.files==0u&&report.media==0u&&
        report.sidecars==0u&&report.nominal_bytes==0u);
 assert(report.failure==(mode==1?
   CMP5_COMPANION_FAILURE_CHANGED:
   CMP5_COMPANION_FAILURE_THUMBNAIL_UNSAFE));
 assert(stat(moved_parent,&st)==0&&S_ISDIR(st.st_mode));
 Cmp5MediaCandidate group[CMP5_GROUP_MAX]={{0}};
 unsigned count=99;
 if(mode==1){
  /* The primary photo parent itself is gone: re-reading it must fail. */
  assert(!cmp5_plan_capture_group(root,&selected,group,&count,NULL,NULL));
  assert(count==0u);
 }else{
  /* A NEW scan after a thumbnail move sees only the known path's
   * absence. It may still inspect the visible JPG/JXR and DAT pair.
   * It cannot infer where Sony may have moved the thumbnail. */
  assert(cmp5_plan_capture_group(root,&selected,group,&count,NULL,NULL));
  assert(count==2u);
  for(unsigned i=0;i<count;i++)assert(group[i].media_type!=4u);
 }
 puts("Renamed Gallery parent and thumbnail parent refused: PASS");
 return 0;
}
