/* Real host-only private backup/restore integration with a thumbnail
 * from the separate PS5 Gallery thumbnails/photo tree. No Gallery writes. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "verified_group.h"
#include "media_snapshot.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void snapshot(Cmp5MediaCandidate*out,const char*file,
                     const char*path,unsigned type){
 struct stat st={0};
 assert(stat(file,&st)==0);
 memset(out,0,sizeof(*out));
 assert(strlen(path)<sizeof(out->preview_path));
 strcpy(out->preview_path,path);
 out->media_type=type;
 out->bytes=(uint64_t)st.st_size;
 cmp5_snapshot_capture(out,&st);
}
static void compare(const char*a,const char*b){
 int lhs=open(a,O_RDONLY|O_NOFOLLOW),rhs=open(b,O_RDONLY|O_NOFOLLOW);
 assert(lhs>=0&&rhs>=0);
 unsigned char la[256],rb[256];
 for(;;){
  ssize_t n=read(lhs,la,sizeof(la));
  ssize_t m=read(rhs,rb,sizeof(rb));
  assert(n>=0&&m==n);
  if(n==0)break;
  assert(memcmp(la,rb,(size_t)n)==0);
 }
 close(lhs);close(rhs);
}
int main(int argc,char**argv){
 if(argc!=7)return 2;
 const char*root=argv[1],*photo=argv[2],*dat=argv[3],
           *thumb=argv[4],*backup=argv[5],*restore=argv[6];
 Cmp5MediaCandidate original={0};
 snapshot(&original,photo,
  "/user/av_contents/photo/NPXS00000/GAME123/REC_001.jpg",1u);
 Cmp5MediaCandidate group[CMP5_GROUP_MAX]={{0}};
 uint32_t count=0;
 assert(cmp5_plan_capture_group(root,&original,group,&count,NULL,NULL));
 assert(count==3u&&cmp5_validate_capture_group(group,count));
 assert(group[0].media_type==3u&&group[1].media_type==1u&&
        group[2].media_type==4u);
 assert(strcmp(group[2].preview_path,
  "/user/av_contents/thumbnails/photo/NPXS00000/GAME123/REC_001.jpg.jpeg")==0);
 Cmp5GroupReceipt result={0},verified={0};
 /* A caller cannot omit a known thumbnail and still commit the group. */
 assert(!cmp5_backup_group(root,group,2u,backup,"OMITTED",&result,NULL,NULL));
 assert(result.count==0u);
 assert(cmp5_backup_group(root,group,count,backup,"THUMB",&result,NULL,NULL));
 assert(result.count==3u);
 assert(cmp5_verify_group(backup,"THUMB",&verified,NULL,NULL));
 assert(verified.count==3u&&verified.bytes==result.bytes&&
        memcmp(verified.manifest_sha256,result.manifest_sha256,32)==0);
 assert(cmp5_restore_group(backup,"THUMB",restore,&verified,NULL,NULL));
 assert(cmp5_verify_restored_group(restore,"THUMB",&verified,NULL,NULL));
 char restored_name[512];
 const char*source_files[3]={dat,photo,thumb};
 for(unsigned i=0;i<3u;i++){
  const int n=snprintf(restored_name,sizeof(restored_name),
    "%s/THUMB__%u",restore,i+1u);
  assert(n>0&&(size_t)n<sizeof(restored_name));
  compare(source_files[i],restored_name);
 }
 puts("Gallery JPG/DAT/external thumbnail: private SHA-256 backup and restore PASS");
 return 0;
}
