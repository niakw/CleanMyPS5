#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "capture_companions.h"
#include "media_snapshot.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
int main(int argc,char**argv){
 if(argc!=5)return 2;
 const char*root=argv[1],*media=argv[2],*relative=argv[3];
 int desired=0;
 const int invalid=!strcmp(argv[4],"unsafe")||!strcmp(argv[4],"unreadable");
 if(!strcmp(argv[4],"found"))desired=CMP5_THUMBNAIL_CANDIDATE_FOUND;
 else if(!strcmp(argv[4],"missing"))desired=CMP5_THUMBNAIL_KNOWN_PATH_ABSENT;
 else if(!strcmp(argv[4],"unsafe"))desired=CMP5_THUMBNAIL_UNSAFE;
 else if(!strcmp(argv[4],"unreadable"))desired=CMP5_THUMBNAIL_INACCESSIBLE;
 else if(!strcmp(argv[4],"not-checked"))desired=CMP5_THUMBNAIL_NOT_CHECKED;
 else return 3;
 struct stat st={0};
 assert(stat(media,&st)==0);
 Cmp5MediaCandidate primary={0};
 assert(strlen(relative)<sizeof(primary.preview_path));
 strcpy(primary.preview_path,relative);
 primary.bytes=(uint64_t)st.st_size;
 primary.media_type=strstr(relative,"/video/")?2u:1u;
 cmp5_snapshot_capture(&primary,&st);
 Cmp5CompanionReport report={0};
 const int accepted=cmp5_inspect_capture_companions(root,&primary,&report,NULL,NULL);
 assert((int)report.thumbnail==desired);
 if(invalid){
  assert(!accepted&&report.state==CMP5_COMPANIONS_UNVERIFIABLE);
  assert(report.files==0u&&report.media==0u&&
         report.sidecars==0u&&report.nominal_bytes==0u);
  assert(report.failure==(!strcmp(argv[4],"unsafe")?
    CMP5_COMPANION_FAILURE_THUMBNAIL_UNSAFE:
    CMP5_COMPANION_FAILURE_THUMBNAIL_UNAVAILABLE));
 }else{
  assert(accepted&&report.state==CMP5_COMPANIONS_MULTIPLE);
  assert(report.files==2u&&report.sidecars==1u&&report.media==1u);
 }
 Cmp5MediaCandidate plan[CMP5_GROUP_MAX];
 memset(plan,0xA5,sizeof(plan));
 uint32_t count=55u;
 const int approved=cmp5_plan_capture_group(root,&primary,plan,&count,NULL,NULL);
 if(invalid){
  assert(!approved&&count==0u);
  const unsigned char*bytes=(const unsigned char*)plan;
  for(size_t i=0;i<sizeof(plan);i++)assert(bytes[i]==0u);
 }else if(desired==CMP5_THUMBNAIL_CANDIDATE_FOUND){
  assert(approved&&count==3u);
  assert(plan[2].media_type==4u&&
    strstr(plan[2].preview_path,"/thumbnails/photo/")!=NULL);
  assert(strstr(plan[2].preview_path,"REC_001.jpg.jpeg")!=NULL);
 }else assert(approved&&count==2u);
 puts("Read-only gallery cross-tree thumbnail evidence: PASS");
 return 0;
}
