/* Real host-only private backup/restore integration with a thumbnail
 * from the separate PS5 Gallery thumbnails/photo tree. No Gallery writes. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "verified_group.h"
#include "verified_backup.h"
#include "media_snapshot.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
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
/* A non-terminated ID immediately before an inaccessible page used to
 * provoke an out-of-bounds read in the backup/group validators. This fixture
 * runs only on the Linux host, and never touches PS5 data or a real backup. */
static void test_unterminated_id(const char*workspace,size_t limit,int group){
 const long raw_page=sysconf(_SC_PAGESIZE);
 assert(raw_page>0&&(size_t)raw_page>limit);
 const size_t page=(size_t)raw_page;
 void*memory=mmap(NULL,page*2u,PROT_READ|PROT_WRITE,
                  MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 assert(memory!=MAP_FAILED);
 assert(mprotect((char*)memory+page,page,PROT_NONE)==0);
 char*id=(char*)memory+page-limit;
 memset(id,'A',limit); /* no NUL before the guard page */
 if(group){
  Cmp5GroupReceipt result={0};
  assert(!cmp5_verify_group(workspace,id,&result,NULL,NULL));
  assert(result.count==0u);
 }else{
  Cmp5BackupReceipt result={0};
  assert(!cmp5_verify_backup(workspace,id,&result,NULL,NULL));
  assert(!result.published);
 }
 assert(munmap(memory,page*2u)==0);
}
int main(int argc,char**argv){
 if(argc!=7)return 2;
 const char*root=argv[1],*photo=argv[2],*dat=argv[3],
           *thumb=argv[4],*backup=argv[5],*restore=argv[6];
 test_unterminated_id(backup,31u,1);
 test_unterminated_id(backup,48u,0);
 Cmp5MediaCandidate original={0};
 snapshot(&original,photo,
  "/user/av_contents/photo/NPXS00000/GAME123/REC_001.jpg",1u);
 Cmp5MediaCandidate group[CMP5_GROUP_MAX]={{0}};
 uint32_t count=0;
 assert(cmp5_plan_capture_group(root,&original,group,&count,NULL,NULL));
 assert(count==3u&&cmp5_validate_capture_group(group,count));
 assert(group[0].media_type==3u&&group[1].media_type==1u&&
        group[2].media_type==4u);
 /* Same relative recording name does not permit mixing photo and video
  * metadata into a misleading "complete" private restore group. */
 Cmp5MediaCandidate mixed[3];
 memcpy(mixed,group,sizeof(mixed));
 mixed[1].media_type=2u;
 strcpy(mixed[1].preview_path,
  "/user/av_contents/video/NPXS00000/GAME123/REC_001.mp4");
 assert(!cmp5_validate_capture_group(mixed,3u));
 assert(strcmp(group[2].preview_path,
  "/user/av_contents/thumbnails/photo/NPXS00000/GAME123/REC_001.jpg.jpeg")==0);
 Cmp5GroupReceipt result={0},verified={0};
 /* A caller cannot omit a known thumbnail and still commit the group. */
 assert(!cmp5_backup_group(root,group,2u,backup,"OMITTED",&result,NULL,NULL));
 assert(result.count==0u);
 assert(cmp5_backup_group(root,group,count,backup,"THUMB",&result,NULL,NULL));
 assert(result.count==3u);
 /* Member order must not affect which photo is selected for coverage. */
 Cmp5MediaCandidate reordered[3]={group[2],group[0],group[1]};
 assert(cmp5_validate_capture_group(reordered,3u));
 Cmp5GroupReceipt reordered_receipt={0};
 assert(cmp5_backup_group(root,reordered,3u,backup,"REORDER",
   &reordered_receipt,NULL,NULL));
 assert(reordered_receipt.count==3u);
 assert(cmp5_verify_group(backup,"REORDER",NULL,NULL,NULL));
 /* The ordered original Gallery locators survive program restart in the
  * private workspace, bound to the same receipt as the three capsules. */
 Cmp5OriginMap origins={0},reordered_origins={0};
 assert(cmp5_read_group_origin_map(backup,"THUMB",&origins,NULL,NULL));
 assert(origins.count==count);
 for(unsigned i=0;i<count;i++){
  assert(strcmp(origins.entries[i].path,group[i].preview_path)==0);
  assert(origins.entries[i].bytes==group[i].bytes);
  assert(origins.entries[i].media_type==group[i].media_type);
 }
 assert(cmp5_read_group_origin_map(backup,"REORDER",&reordered_origins,
                                  NULL,NULL));
 for(unsigned i=0;i<count;i++)
  assert(strcmp(reordered_origins.entries[i].path,
                reordered[i].preview_path)==0);
 /* A different ordered map cannot be trusted just because the capsules
  * hash correctly: reject tampered path metadata before restoration. */
 char map_path[512];
 assert(snprintf(map_path,sizeof(map_path),"%s/REORDER__ORIGINS",backup)>0);
 int map_fd=open(map_path,O_RDWR|O_NOFOLLOW);
 assert(map_fd>=0);
 struct stat map_stat={0};
 assert(fstat(map_fd,&map_stat)==0&&(map_stat.st_mode&077)==0);
 assert(pwrite(map_fd,"X",1,44)==1);
 assert(close(map_fd)==0);
 assert(!cmp5_verify_group(backup,"REORDER",NULL,NULL,NULL));
 assert(!cmp5_read_group_origin_map(backup,"REORDER",
                                    &reordered_origins,NULL,NULL));
 Cmp5RecoveryReport broken={0};
 assert(cmp5_inspect_group_recovery(backup,"REORDER",&broken));
 assert(broken.state==CMP5_RECOVERY_INVALID);
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
