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
 assert(cmp5_inspect_capture_companions(root,&primary,&report,NULL,NULL)==1);
 assert(report.state==CMP5_COMPANIONS_MULTIPLE);
 assert(report.files==2u&&report.sidecars==1u&&report.media==1u);
 assert((int)report.thumbnail==desired);
 puts("Read-only gallery cross-tree thumbnail evidence: PASS");
 return 0;
}
