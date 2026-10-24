/* Host-only deterministic mutation of a committed manifest and restored
 * member. Include the real implementation to intercept its read() calls. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static char watched[1024],moved[1024];
static int action,triggered,fail_staging_unlink;
static int racing_unlinkat(int dir,const char*name,int flags){
 if(fail_staging_unlink&&!strcmp(name,".STAGE__COMMIT.part")){
  errno=EIO;return -1;
 }
 return unlinkat(dir,name,flags);
}
static ssize_t race_read(int fd,void*out,size_t n){
 ssize_t got=read(fd,out,n);
 if(got>0&&action&&!triggered){
  struct stat opened,current;
  if(fstat(fd,&opened)==0&&stat(watched,&current)==0&&
     opened.st_dev==current.st_dev&&opened.st_ino==current.st_ino){
   if(action==1){
    if(rename(watched,moved)!=0){perror("rename race");exit(15);}
   }else if(action==2){
    if(chmod(watched,(current.st_mode&07777)^S_IXUSR)!=0){
     perror("chmod race");exit(16);
    }
   }else if(action==3){
    if(link(watched,moved)!=0){perror("hardlink race");exit(17);}
   }
   triggered=1;
  }
 }
 return got;
}
static int directory_scan_swap;
static char scan_old_dir[1024],scan_new_dir[1024];
static struct dirent*racing_readdir(DIR*dir){
 struct dirent*entry=readdir(dir);
 if(directory_scan_swap&&entry){
  assert(rename(scan_old_dir,scan_new_dir)==0);
  assert(mkdir(scan_old_dir,0700)==0);
  directory_scan_swap=0;
 }
 return entry;
}
#define read race_read
#define readdir racing_readdir
#define unlinkat racing_unlinkat
#include "../src/native/verified_group.c"
#undef unlinkat
#undef readdir
#undef read
static void setup_watch(const char*parent,const char*name,const char*replacement,int operation){
 int length=snprintf(watched,sizeof(watched),"%s/%s",parent,name);
 assert(length>0&&(size_t)length<sizeof(watched));
 length=snprintf(moved,sizeof(moved),"%s/%s",parent,replacement);
 assert(length>0&&(size_t)length<sizeof(moved));
 action=operation;triggered=0;
}
static int remove_marker_during_member_verification(void*context){
 unsigned*counter=(unsigned*)context;
 if(++(*counter)==2u){
  if(rename(watched,moved)!=0){perror("mid-verification rename");exit(19);}
  triggered=1;
 }
 return 0; /* Do not cancel: identity checks must detect the rename. */
}
/* Rename the selected workspace while a verifier still holds a valid
 * descriptor to it, then recreate an empty directory at the OLD name. */
typedef struct {
 const char*original;
 char relocated[1024];
 unsigned calls;
 int moved;
} WorkspaceSwap;
static int swap_workspace_during_verify(void*ctx){
 WorkspaceSwap*state=(WorkspaceSwap*)ctx;
 if(++state->calls==2u){
  int n=snprintf(state->relocated,sizeof(state->relocated),
    "%s.relocated",state->original);
  assert(n>0&&(size_t)n<sizeof(state->relocated));
  assert(rename(state->original,state->relocated)==0);
  assert(mkdir(state->original,0700)==0);
  state->moved=1;
 }
 return 0; /* Do not cancel: the verifier must see the changed path itself. */
}
static void write_member(int dir,const char*filename,const void*data,size_t len){
 int fd=openat(dir,filename,O_CREAT|O_WRONLY|O_EXCL,0600);
 assert(fd>=0);
 assert(write_exact(fd,(const uint8_t*)data,len));
 assert(close(fd)==0);
}
/* Synthetic self-verifying capsule: no real capture files or Gallery APIs. */
static void write_capsule(int dir,const char*filename,
 const uint8_t*body,size_t len,const uint8_t digest[32]){
 uint8_t header[64]={0};
 memcpy(header,"CMP5BAK1",8);
 u64(header+8,len);
 memcpy(header+16,digest,32);
 int fd=openat(dir,filename,O_CREAT|O_WRONLY|O_EXCL,0600);
 assert(fd>=0);
 assert(write_exact(fd,header,sizeof(header)));
 assert(write_exact(fd,body,len));
 assert(close(fd)==0);
}
int main(int argc,char**argv){
 if(argc!=2)return 2;
 const char*workspace=argv[1];
 int dir=safe_dir(workspace);
 assert(dir>=0);
 const uint8_t one[]="SYNTHETIC_FIRST_MEMBER";
 const uint8_t two[]="SYNTHETIC_SECOND_MEMBER";
 const uint8_t *data[2]={one,two};
 const size_t lens[2]={sizeof(one)-1,sizeof(two)-1};
 Cmp5BackupReceipt receipts[2]={{0}};
 for(unsigned i=0;i<2;i++){
  receipts[i].bytes=lens[i];
  hash(data[i],lens[i],receipts[i].sha256);
  char name[48];member_name(name,"ALBUM",i);
  write_member(dir,name,data[i],lens[i]);
 }
 uint8_t manifest[MARK_SIZE];
 manifest_header(manifest,receipts,2);
 write_member(dir,"ALBUM__RESTORED",manifest,sizeof(manifest));
 uint8_t buffer[MARK_SIZE];
 Cmp5BackupReceipt found[CMP5_GROUP_MAX]={{0}};
 uint32_t count=0;
 Cmp5GroupReceipt result={0};
 assert(read_manifest(dir,"ALBUM",1,buffer,found,&count,&result));
 assert(count==2&&result.bytes==lens[0]+lens[1]);
 assert(raw_restored(dir,"ALBUM__1",lens[0],receipts[0].sha256,NULL,NULL));
 setup_watch(workspace,"ALBUM__RESTORED","MOVED__RESTORED",1);
 assert(!read_manifest(dir,"ALBUM",1,buffer,found,&count,&result));
 assert(triggered);
 assert(rename(moved,watched)==0);
 setup_watch(workspace,"ALBUM__RESTORED","UNUSED__RESTORED",2);
 assert(!read_manifest(dir,"ALBUM",1,buffer,found,&count,&result));
 assert(triggered);
 setup_watch(workspace,"ALBUM__RESTORED","LINKED__RESTORED",3);
 assert(!read_manifest(dir,"ALBUM",1,buffer,found,&count,&result));
 assert(triggered);
 assert(unlink(moved)==0);
 setup_watch(workspace,"ALBUM__1","MOVED__1",1);
 assert(!raw_restored(dir,"ALBUM__1",lens[0],receipts[0].sha256,NULL,NULL));
 assert(triggered);
 assert(rename(moved,watched)==0);
 setup_watch(workspace,"ALBUM__1","HARDLINK__1",3);
 assert(!raw_restored(dir,"ALBUM__1",lens[0],receipts[0].sha256,NULL,NULL));
 assert(triggered);
 assert(unlink(moved)==0);
 action=0;
 assert(raw_restored(dir,"ALBUM__1",lens[0],receipts[0].sha256,NULL,NULL));
 setup_watch(workspace,"ALBUM__RESTORED","LATE__RESTORED",0);
 unsigned calls=0;
 assert(!cmp5_verify_restored_group(workspace,"ALBUM",&result,
   remove_marker_during_member_verification,&calls));
 assert(triggered&&calls>=2u);
 assert(rename(moved,watched)==0);
 assert(cmp5_verify_restored_group(workspace,"ALBUM",&result,NULL,NULL));
 assert(result.count==2u&&result.bytes==lens[0]+lens[1]);
 WorkspaceSwap swapped={0};swapped.original=workspace;
 assert(!cmp5_verify_restored_group(workspace,"ALBUM",&result,
    swap_workspace_during_verify,&swapped));
 assert(swapped.moved&&swapped.calls>=2u);
 /* Restore the exact original, never silently operate on its replacement. */
 assert(rmdir(workspace)==0);
 assert(rename(swapped.relocated,workspace)==0);
 assert(cmp5_verify_restored_group(workspace,"ALBUM",&result,NULL,NULL));
 for(unsigned i=0;i<2;i++){
  char name[48];member_name(name,"BACKUP",i);
  write_capsule(dir,name,data[i],lens[i],receipts[i].sha256);
 }
 write_member(dir,"BACKUP__COMMIT",manifest,sizeof(manifest));
 assert(cmp5_verify_group(workspace,"BACKUP",&result,NULL,NULL));
 setup_watch(workspace,"BACKUP__COMMIT","BACKUP_MOVED",0);
 calls=0;
 assert(!cmp5_verify_group(workspace,"BACKUP",&result,
   remove_marker_during_member_verification,&calls));
 assert(triggered&&calls>=2u);
 assert(rename(moved,watched)==0);
 assert(cmp5_verify_group(workspace,"BACKUP",&result,NULL,NULL));
 assert(result.count==2u&&result.bytes==lens[0]+lens[1]);
 /* A failed cleanup after atomic publication must not yield a success:
  * the private marker and staged hardlink may both remain for recovery. */
 fail_staging_unlink=1;
 assert(!publish_manifest(dir,"STAGE",0,manifest,NULL,NULL));
 fail_staging_unlink=0;
 struct stat st;
 assert(fstatat(dir,"STAGE__COMMIT",&st,AT_SYMLINK_NOFOLLOW)==0);
 assert(fstatat(dir,".STAGE__COMMIT.part",&st,AT_SYMLINK_NOFOLLOW)==0);
 assert(unlinkat(dir,".STAGE__COMMIT.part",0)==0);
 assert(unlinkat(dir,"STAGE__COMMIT",0)==0);
 /* Regression: the private workspace's anchored descriptor was already
  * used for a complete directory scan and has a shared cursor at EOF.
  * Repeating the audit MUST start at the beginning independently, or a
  * surplus capsule can evade detection.
  * The test deliberately drains a DUP of the SAME descriptor. */
 write_member(dir,"BACKUP__5","unknown",7);
 write_member(dir,"ALBUM__5","unknown",7);
 int cursor=dup(dir);assert(cursor>=0);
 DIR*cursor_stream=fdopendir(cursor);assert(cursor_stream);
 while(readdir(cursor_stream)){}
 assert(closedir(cursor_stream)==0);
 uint32_t unexpected=0;
 assert(inspect_namespace(dir,"BACKUP",0,&unexpected));
 assert(unexpected==1u);
 assert(!empty_group_namespace(dir,"ALBUM"));
 assert(unlinkat(dir,"BACKUP__5",0)==0);
 assert(unlinkat(dir,"ALBUM__5",0)==0);
 /* A scan of an empty group can otherwise return EMPTY for an orphaned
  * directory that is no longer present under the caller's workspace name. */
 int n=snprintf(scan_old_dir,sizeof(scan_old_dir),"%s",workspace);
 assert(n>0&&(size_t)n<sizeof(scan_old_dir));
 n=snprintf(scan_new_dir,sizeof(scan_new_dir),"%s.moved",workspace);
 assert(n>0&&(size_t)n<sizeof(scan_new_dir));
 directory_scan_swap=1;
 Cmp5RecoveryReport scan_report={0};
 assert(!cmp5_inspect_group_recovery(workspace,"UNKNOWN",&scan_report));
 assert(!directory_scan_swap);
 assert(scan_report.state==0);
 assert(rmdir(workspace)==0);
 assert(rename(scan_new_dir,workspace)==0);
 assert(cmp5_inspect_group_recovery(workspace,"UNKNOWN",&scan_report));
 assert(scan_report.state==CMP5_RECOVERY_EMPTY);
 assert(close(dir)==0);
 puts("Private group manifest and restored media races refused: PASS");
 return 0;
}
