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
static int action,triggered,fail_staging_unlink,swap_stage_on_fsync;
static int owner_fault,owner_after_read,owner_fault_hits;
/* Host-only fstat fault: another owner AFTER the first body read.
 * No on-disk chown is performed, so tests are safe when run as root. */
static int racing_fstat(int fd,struct stat*st){
 const int rc=fstat(fd,st);
 if(rc==0&&owner_fault&&owner_after_read&&S_ISREG(st->st_mode)){
  struct stat current={0};
  if(stat(watched,&current)==0&&
     st->st_dev==current.st_dev&&st->st_ino==current.st_ino){
   st->st_uid=(uid_t)(st->st_uid+1u);
   ++owner_fault_hits;
  }
 }
 return rc;
}
/* Swap the just-written temporary between fsync and linkat. The
 * group publisher must neither commit a foreign inode nor remove it. */
static int racing_fsync(int fd){
 const int result=fsync(fd);
 if(result==0&&swap_stage_on_fsync){
  struct stat original={0},named={0};
  if(fstat(fd,&original)==0&&stat(watched,&named)==0&&
     original.st_dev==named.st_dev&&original.st_ino==named.st_ino){
   assert(rename(watched,moved)==0);
   int foreign=open(watched,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
   assert(foreign>=0);
   static const char contents[]="FOREIGN_COMMIT_STAGE_DO_NOT_DELETE";
   assert(write(foreign,contents,sizeof(contents)-1u)==
          (ssize_t)(sizeof(contents)-1u));
   assert(close(foreign)==0);
   triggered=1;swap_stage_on_fsync=0;
  }
 }
 return result;
}
static int racing_unlinkat(int dir,const char*name,int flags){
 if(fail_staging_unlink&&!strcmp(name,".STAGE__COMMIT.part")){
  errno=EIO;return -1;
 }
 return unlinkat(dir,name,flags);
}
static ssize_t race_read(int fd,void*out,size_t n){
 ssize_t got=read(fd,out,n);
 if(got>0&&owner_fault){
  struct stat opened={0},current={0};
  if(fstat(fd,&opened)==0&&stat(watched,&current)==0&&
     opened.st_dev==current.st_dev&&opened.st_ino==current.st_ino)
   owner_after_read=1;
 }
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
/* Simulate rename/replacement after opening the original capture inode,
 * immediately before the final pathname identity check. */
static int origin_name_swap=0;
static char origin_parent_path[1024],origin_parent_moved[1024];
static int origin_parent_swap_on_named=0,origin_parent_swap_on_missing=0;
static void move_origin_parent(void){
 assert(rename(origin_parent_path,origin_parent_moved)==0);
 assert(mkdir(origin_parent_path,0700)==0);
 triggered=1;
}
static int racing_openat(int dir,const char*name,int flags,...){
 if(origin_parent_swap_on_missing&&!strcmp(name,"MISSING.jpg")){
  origin_parent_swap_on_missing=0;
  move_origin_parent();
 }
 return openat(dir,name,flags,0600);
}
static int racing_fstatat(int dir,const char*name,struct stat*st,int flags){
 if(origin_parent_swap_on_named&&!strcmp(name,"ORIGINAL.jpg")){
  origin_parent_swap_on_named=0;
  move_origin_parent();
 }
 if(origin_name_swap&&strcmp(name,"ORIGINAL.jpg")==0){
  const int replace=origin_name_swap==2;
  assert(rename(watched,moved)==0);
  if(replace){
   int replacement=open(watched,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
   assert(replacement>=0);
   static const char new_data[]="FOREIGN_CAPTURE";
   assert(write(replacement,new_data,sizeof(new_data)-1u)==
          (ssize_t)(sizeof(new_data)-1u));
   assert(close(replacement)==0);
  }
  origin_name_swap=0;triggered=1;
 }
 return fstatat(dir,name,st,flags);
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
#define fstat racing_fstat
#define readdir racing_readdir
#define unlinkat racing_unlinkat
#define fsync racing_fsync
#define fstatat racing_fstatat
#define openat racing_openat
#include "../src/native/verified_group.c"
#undef openat
#undef fstatat
#undef fsync
#undef unlinkat
#undef readdir
#undef fstat
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
/* Simulate an uncooperative writer switching the temporary group
 * marker BEFORE our cancellation callback returns. The publisher must
 * not unlink the foreign replacement while reporting an aborted group. */
static int swap_cancelled_marker(void*context){
 const int dir=*(const int*)context;
 assert(renameat(dir,".CANCEL__COMMIT.part",
                 dir,".CANCEL__COMMIT.original")==0);
 const int replacement=openat(dir,".CANCEL__COMMIT.part",
                              O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
 assert(replacement>=0);
 static const char foreign[]="KEEP FOREIGN TEMPORARY";
 assert(write(replacement,foreign,sizeof(foreign)-1u)==
        (ssize_t)(sizeof(foreign)-1u));
 assert(close(replacement)==0);
 return 1;
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
 /* A manifest that changes owner while being read cannot be certified,
  * even if inode, size, bytes and times are otherwise identical. */
 setup_watch(workspace,"ALBUM__RESTORED","OWNER_UNUSED",0);
 owner_fault=1;owner_after_read=0;owner_fault_hits=0;
 assert(!read_manifest(dir,"ALBUM",1,buffer,found,&count,&result));
 assert(owner_after_read&&owner_fault_hits>0);
 owner_fault=0;owner_after_read=0;
 assert(read_manifest(dir,"ALBUM",1,buffer,found,&count,&result));
 /* The same invariant applies to independently hashed RAW members. */
 setup_watch(workspace,"ALBUM__1","OWNER_UNUSED",0);
 owner_fault=1;owner_after_read=0;owner_fault_hits=0;
 assert(!raw_restored(dir,"ALBUM__1",lens[0],receipts[0].sha256,NULL,NULL));
 assert(owner_after_read&&owner_fault_hits>0);
 owner_fault=0;owner_after_read=0;
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
 /* Legacy digest-only backup has no persistent member-to-source map.
  * It may be byte-identical, but is not a restorable verified album. */
 assert(!cmp5_verify_group(workspace,"BACKUP",&result,NULL,NULL));
 Cmp5MediaCandidate origin_parts[2]={{0}};
 strcpy(origin_parts[0].preview_path,
  "/user/av_contents/photo/NPXS00000/GAME123/REC_001.jpg");
 strcpy(origin_parts[1].preview_path,
  "/user/av_contents/photo/NPXS00000/GAME123/REC_001.dat");
 for(unsigned i=0;i<2;i++){
  origin_parts[i].bytes=(uint64_t)lens[i];
  origin_parts[i].media_type=i==0u?1u:3u;
  origin_parts[i].snapshot_device=1u;
  origin_parts[i].snapshot_inode=(uint64_t)i+1u;
  origin_parts[i].modified_at=1700000000;
  origin_parts[i].snapshot_ctime=1700000001;
  origin_parts[i].snapshot_mtime_nsec=123456789u;
  origin_parts[i].snapshot_ctime_nsec=987654321u;
 }
 assert(cmp5_validate_capture_group(origin_parts,2u));
 assert(publish_origin_map(dir,"BACKUP",origin_parts,2u,manifest,NULL,NULL));
 assert(cmp5_verify_group(workspace,"BACKUP",&result,NULL,NULL));
 /* The private origin map includes sensitive locators. Ownership must
  * remain stable through the actual map read before group verification. */
 setup_watch(workspace,"BACKUP__ORIGINS","OWNER_UNUSED",0);
 owner_fault=1;owner_after_read=0;owner_fault_hits=0;
 assert(!cmp5_verify_group(workspace,"BACKUP",&result,NULL,NULL));
 assert(owner_after_read&&owner_fault_hits>0);
 owner_fault=0;owner_after_read=0;
 assert(cmp5_verify_group(workspace,"BACKUP",&result,NULL,NULL));
 /* A valid origin-map digest does not excuse a pathname switch, chmod or
  * second hardlink while its bytes are read. Revalidate the opened inode. */
 setup_watch(workspace,"BACKUP__ORIGINS","BACKUP_ORIGINS_MOVED",1);
 assert(!cmp5_verify_group(workspace,"BACKUP",&result,NULL,NULL));
 assert(triggered);
 assert(rename(moved,watched)==0);
 setup_watch(workspace,"BACKUP__ORIGINS","BACKUP_ORIGINS_UNUSED",2);
 assert(!cmp5_verify_group(workspace,"BACKUP",&result,NULL,NULL));
 assert(triggered);
 assert(chmod(watched,0600)==0);
 setup_watch(workspace,"BACKUP__ORIGINS","BACKUP_ORIGINS_LINK",3);
 assert(!cmp5_verify_group(workspace,"BACKUP",&result,NULL,NULL));
 assert(triggered);
 assert(unlink(moved)==0);
 action=0;
 assert(cmp5_verify_group(workspace,"BACKUP",&result,NULL,NULL));
 /* A valid origin map may have been published before a power loss,
  * without the final COMMIT. It is INCOMPLETE, never EMPTY. */
 Cmp5RecoveryReport origin_report={0};
 assert(publish_origin_map(dir,"ORPHAN",origin_parts,2u,manifest,NULL,NULL));
 assert(cmp5_inspect_group_recovery(workspace,"ORPHAN",&origin_report));
 assert(origin_report.state==CMP5_RECOVERY_INCOMPLETE);
 assert(origin_report.published_parts==0u&&!origin_report.marker_present);
 assert(origin_report.origin_map_present&&!origin_report.origin_map_staged);
 /* A lone temporary origin mapping is also evidence of interruption. */
 write_member(dir,".STAGED__ORIGINS.part","INCOMPLETE",10u);
 assert(cmp5_inspect_group_recovery(workspace,"STAGED",&origin_report));
 assert(origin_report.state==CMP5_RECOVERY_INCOMPLETE);
 assert(origin_report.staged_parts==1u);
 assert(!origin_report.origin_map_present&&origin_report.origin_map_staged);
 /* An invalid/oversized map cannot masquerade as an incomplete but
  * otherwise trustworthy published record. */
 write_member(dir,"INVALID__ORIGINS","CORRUPT",7u);
 assert(cmp5_inspect_group_recovery(workspace,"INVALID",&origin_report));
 assert(origin_report.state==CMP5_RECOVERY_INVALID);
 assert(origin_report.origin_map_present&&!origin_report.origin_map_staged);
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
 /* Reject a marker whose .part is replaced immediately after fsync.
  * Both the foreign stage and the original renamed inode must survive. */
 setup_watch(workspace,".SWAPPED__COMMIT.part",
                         ".SWAPPED__COMMIT.original",0);
 swap_stage_on_fsync=1;triggered=0;
 assert(!publish_manifest(dir,"SWAPPED",0,manifest,NULL,NULL));
 assert(triggered&&!swap_stage_on_fsync);
 char foreign_stage[64]={0};
 int foreign_fd=openat(dir,".SWAPPED__COMMIT.part",O_RDONLY|O_NOFOLLOW);
 assert(foreign_fd>=0);
 assert(read(foreign_fd,foreign_stage,sizeof(foreign_stage))==
        (ssize_t)(sizeof("FOREIGN_COMMIT_STAGE_DO_NOT_DELETE")-1u));
 assert(strcmp(foreign_stage,"FOREIGN_COMMIT_STAGE_DO_NOT_DELETE")==0);
 assert(close(foreign_fd)==0);
 struct stat stage_stat={0};
 assert(fstatat(dir,"SWAPPED__COMMIT",&stage_stat,AT_SYMLINK_NOFOLLOW)<0&&
        errno==ENOENT);
 assert(fstatat(dir,".SWAPPED__COMMIT.original",&stage_stat,
                AT_SYMLINK_NOFOLLOW)==0&&
        stage_stat.st_size==(off_t)MARK_SIZE);
 assert(unlinkat(dir,".SWAPPED__COMMIT.part",0)==0);
 assert(unlinkat(dir,".SWAPPED__COMMIT.original",0)==0);
 /* Aborted publish must preserve a stage name swapped before cleanup. */
 assert(!publish_manifest(dir,"CANCEL",0,manifest,
                           swap_cancelled_marker,&dir));
 char foreign[64]={0};
 int marker=openat(dir,".CANCEL__COMMIT.part",O_RDONLY|O_NOFOLLOW);
 assert(marker>=0);
 assert(read(marker,foreign,sizeof(foreign))==
        (ssize_t)(sizeof("KEEP FOREIGN TEMPORARY")-1u));
 assert(strcmp(foreign,"KEEP FOREIGN TEMPORARY")==0);
 assert(close(marker)==0);
 struct stat st;
 assert(fstatat(dir,"CANCEL__COMMIT",&st,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT);
 assert(unlinkat(dir,".CANCEL__COMMIT.part",0)==0);
 assert(unlinkat(dir,".CANCEL__COMMIT.original",0)==0);
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
 /* Host-only synthetic original: opening it is not enough unless
  * the final directory entry still names that opened file. */
 char origin_tmp[]="/tmp/cmp5-origin-name-XXXXXX";
 char*origin_root=mkdtemp(origin_tmp);
 assert(origin_root!=NULL);
 assert(chmod(origin_root,0700)==0);
 int origin_fd=open(origin_root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 assert(origin_fd>=0);
 int origin_file=openat(origin_fd,"ORIGINAL.jpg",
                        O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
 assert(origin_file>=0);
 static const char image_data[]="UNCHANGED_TEST_CAPTURE";
 assert(write(origin_file,image_data,sizeof(image_data)-1u)==
        (ssize_t)(sizeof(image_data)-1u));
 assert(close(origin_file)==0);
 struct stat live={0};
 assert(fstatat(origin_fd,"ORIGINAL.jpg",&live,AT_SYMLINK_NOFOLLOW)==0);
 Cmp5OriginEntry original_entry={0};
 strcpy(original_entry.path,"/ORIGINAL.jpg");
 original_entry.bytes=(uint64_t)live.st_size;
 original_entry.media_type=1u;
 original_entry.snapshot_device=(uint64_t)live.st_dev;
 original_entry.snapshot_inode=(uint64_t)live.st_ino;
 original_entry.snapshot_ctime=(int64_t)live.st_ctime;
 original_entry.modified_at=(int64_t)live.st_mtime;
 original_entry.snapshot_mtime_nsec=cmp5_mtime_ns(&live);
 original_entry.snapshot_ctime_nsec=cmp5_ctime_ns(&live);
 assert(inspect_source_once(origin_fd,&original_entry)==
        CMP5_ORIGIN_UNCHANGED);
 assert(snprintf(watched,sizeof(watched),"%s/ORIGINAL.jpg",origin_root)>0);
 assert(snprintf(moved,sizeof(moved),"%s/ORIGINAL.moved",origin_root)>0);
 origin_name_swap=1;triggered=0;
 assert(inspect_source_once(origin_fd,&original_entry)==CMP5_ORIGIN_UNKNOWN);
 assert(triggered&&!origin_name_swap);
 assert(rename(moved,watched)==0);
 assert(fstatat(origin_fd,"ORIGINAL.jpg",&live,AT_SYMLINK_NOFOLLOW)==0);
 original_entry.snapshot_ctime=(int64_t)live.st_ctime;
 original_entry.modified_at=(int64_t)live.st_mtime;
 original_entry.snapshot_mtime_nsec=cmp5_mtime_ns(&live);
 original_entry.snapshot_ctime_nsec=cmp5_ctime_ns(&live);
 assert(inspect_source_once(origin_fd,&original_entry)==
        CMP5_ORIGIN_UNCHANGED);
 origin_name_swap=2;triggered=0;
 assert(inspect_source_once(origin_fd,&original_entry)==CMP5_ORIGIN_UNKNOWN);
 assert(triggered&&!origin_name_swap);
 assert(unlink(watched)==0);
 assert(unlink(moved)==0);
 /* A missing pathname cannot be reported as missing if its parent was
  * renamed/replaced while the anchored fd survived the path change.
  * Nor can a previously opened original be called UNCHANGED. */
 assert(snprintf(origin_parent_path,sizeof(origin_parent_path),
                 "%s/ALBUM",origin_root)>0);
 assert(snprintf(origin_parent_moved,sizeof(origin_parent_moved),
                 "%s/ALBUM.moved",origin_root)>0);
 assert(mkdir(origin_parent_path,0700)==0);
 int origin_parent_fd=openat(origin_fd,"ALBUM",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 assert(origin_parent_fd>=0);
 int nested_fd=openat(origin_parent_fd,"ORIGINAL.jpg",
                      O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
 assert(nested_fd>=0);
 assert(write(nested_fd,image_data,sizeof(image_data)-1u)==
        (ssize_t)(sizeof(image_data)-1u));
 assert(close(nested_fd)==0);
 struct stat nested_stat={0};
 assert(fstatat(origin_parent_fd,"ORIGINAL.jpg",
                &nested_stat,AT_SYMLINK_NOFOLLOW)==0);
 Cmp5OriginEntry nested_origin={0};
 strcpy(nested_origin.path,"/ALBUM/ORIGINAL.jpg");
 nested_origin.bytes=(uint64_t)nested_stat.st_size;
 nested_origin.media_type=1u;
 nested_origin.snapshot_device=(uint64_t)nested_stat.st_dev;
 nested_origin.snapshot_inode=(uint64_t)nested_stat.st_ino;
 nested_origin.snapshot_ctime=(int64_t)nested_stat.st_ctime;
 nested_origin.modified_at=(int64_t)nested_stat.st_mtime;
 nested_origin.snapshot_mtime_nsec=cmp5_mtime_ns(&nested_stat);
 nested_origin.snapshot_ctime_nsec=cmp5_ctime_ns(&nested_stat);
 assert(inspect_source_once(origin_fd,&nested_origin)==
        CMP5_ORIGIN_UNCHANGED);
 origin_parent_swap_on_named=1;triggered=0;
 assert(inspect_source_once(origin_fd,&nested_origin)==
        CMP5_ORIGIN_UNKNOWN);
 assert(triggered&&!origin_parent_swap_on_named);
 assert(rmdir(origin_parent_path)==0);
 assert(rename(origin_parent_moved,origin_parent_path)==0);
 assert(inspect_source_once(origin_fd,&nested_origin)==
        CMP5_ORIGIN_UNCHANGED);
 Cmp5OriginEntry missing=nested_origin;
 strcpy(missing.path,"/ALBUM/MISSING.jpg");
 assert(inspect_source_once(origin_fd,&missing)==CMP5_ORIGIN_MISSING);
 origin_parent_swap_on_missing=1;triggered=0;
 assert(inspect_source_once(origin_fd,&missing)==CMP5_ORIGIN_UNKNOWN);
 assert(triggered&&!origin_parent_swap_on_missing);
 assert(rmdir(origin_parent_path)==0);
 assert(rename(origin_parent_moved,origin_parent_path)==0);
 assert(inspect_source_once(origin_fd,&missing)==CMP5_ORIGIN_MISSING);
 assert(unlinkat(origin_parent_fd,"ORIGINAL.jpg",0)==0);
 assert(close(origin_parent_fd)==0);
 assert(rmdir(origin_parent_path)==0);
 assert(close(origin_fd)==0);
 assert(rmdir(origin_root)==0);
 assert(close(dir)==0);
 puts("Private group manifest and restored media races refused: PASS");
 return 0;
}
