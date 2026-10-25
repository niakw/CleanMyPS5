#define _DEFAULT_SOURCE
/* Deterministic host-only races against the real backup implementation.
 * Renames are injected after a successful read from the watched file. */
/* NDEBUG must never disable regression checks in a Release runner. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char watching[1024],moved[1024];
static int armed,triggered,restore_only,fail_directory_fsync;
static int corrupt_published_restore;
static int replace_stage,stage_replaced,post_sync_swap;
static char watched_stage[1024],moved_stage[1024];
static const char stage_sentinel[]="UNRELATED PRIVATE FILE - NEVER DELETE";

static int intercepted_fsync(int fd){
 struct stat st;
 if(replace_stage&&!stage_replaced&&fstat(fd,&st)==0&&
    S_ISREG(st.st_mode)){
  struct stat named;
  if(stat(watched_stage,&named)==0&&
     st.st_dev==named.st_dev&&st.st_ino==named.st_ino){
   assert(rename(watched_stage,moved_stage)==0);
   int decoy=open(watched_stage,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
   assert(decoy>=0);
   assert(write(decoy,stage_sentinel,sizeof(stage_sentinel)-1u)==
          (ssize_t)(sizeof(stage_sentinel)-1u));
   assert(close(decoy)==0);
   stage_replaced=1;
   if(!post_sync_swap){errno=EIO;return -1;}
   /* Unlike injected EIO, this swap leaves fsync successful and
    * exercises the inode binding inside publish(). */
  }
 }
 if(fail_directory_fsync&&fstat(fd,&st)==0&&S_ISDIR(st.st_mode)){
  errno=EIO;return -1;
 }
 return fsync(fd);
}
static ssize_t racing_read(int fd,void*buffer,size_t n){
 const ssize_t got=read(fd,buffer,n);
 if(got>0&&armed&&!triggered){
  struct stat current,expected;
  if(fstat(fd,&current)==0&&stat(watching,&expected)==0&&
     current.st_dev==expected.st_dev&&current.st_ino==expected.st_ino){
   if(rename(watching,moved)!=0){perror("race rename");exit(9);}
   triggered=1;
  }
 }
 return got;
}
static int racing_linkat(int olddir,const char*oldname,
                         int newdir,const char*newname,int flags){
 const int rc=linkat(olddir,oldname,newdir,newname,flags);
 if(rc==0&&corrupt_published_restore&&
    (!strcmp(newname,"CORRUPT")||!strcmp(newname,"BROKEN"))){
  int fd=openat(newdir,newname,O_WRONLY|O_NOFOLLOW);
  assert(fd>=0);
  assert(pwrite(fd,"X",1,0)==1);
  assert(close(fd)==0);
 }
 return rc;
}
static off_t racing_lseek(int fd,off_t n,int whence){
 off_t position=lseek(fd,n,whence);
 if(restore_only&&position==64&&whence==SEEK_SET)armed=1;
 return position;
}
typedef struct{
 const char*old_path;
 const char*new_path;
 unsigned calls;
 int changed;
} SwapWorkspace;
static int change_workspace_during_restore(void*ctx){
 SwapWorkspace*state=(SwapWorkspace*)ctx;
 if(armed&&++state->calls==2u){
  assert(rename(state->old_path,state->new_path)==0);
  assert(mkdir(state->old_path,0700)==0);
  state->changed=1;
 }
 return 0;
}
static int change_workspace_during_read(void*ctx){
 SwapWorkspace*state=(SwapWorkspace*)ctx;
 if(++state->calls==2u){
  assert(rename(state->old_path,state->new_path)==0);
  assert(mkdir(state->old_path,0700)==0);
  state->changed=1;
 }
 return 0;
}
#define read racing_read
#define linkat racing_linkat
#define lseek racing_lseek
#define fsync intercepted_fsync
#include "../src/native/verified_backup.c"
#undef fsync
#undef lseek
#undef linkat
#undef read

int main(int argc,char**argv){
 if(argc!=7)return 2;
 const char*root=argv[1],*original=argv[2],*backup=argv[3];
 const char*restore=argv[4],*action=argv[5],*new_path=argv[6];
 Cmp5MediaCandidate item={0};
 struct stat st;
 assert(stat(original,&st)==0);
 item.media_type=1;
 item.bytes=(uint64_t)st.st_size;
 cmp5_snapshot_capture(&item,&st);
 strcpy(item.preview_path,"/user/av_contents/photo/NPXS00000/GAME123/CAPTURE.JPG");
 Cmp5BackupReceipt receipt={0};
 if(!strcmp(action,"create")){
  assert(cmp5_backup_media(root,&item,backup,"GOOD",&receipt,NULL,NULL));
  assert(receipt.published&&receipt.bytes==item.bytes);
 }else if(!strcmp(action,"backup-workspace-swap")){
  SwapWorkspace state={backup,new_path,0u,0};
  assert(!cmp5_backup_media(root,&item,backup,"SWAPPED",&receipt,
    change_workspace_during_read,&state));
  assert(state.changed&&!receipt.published);
 }else if(!strcmp(action,"backup-stage-swapped")){
  assert(snprintf(watched_stage,sizeof(watched_stage),
                  "%s/.INJECT.part",backup)>0);
  assert(snprintf(moved_stage,sizeof(moved_stage),
                  "%s/.INJECT.original",backup)>0);
  replace_stage=1;
  assert(!cmp5_backup_media(root,&item,backup,"INJECT",&receipt,NULL,NULL));
  assert(stage_replaced&&!receipt.published);
  assert(access(watched_stage,F_OK)==0&&access(moved_stage,F_OK)==0);
 }else if(!strcmp(action,"backup-stage-postfsync-swapped")){
  assert(snprintf(watched_stage,sizeof(watched_stage),
                  "%s/.POSTSAVE.part",backup)>0);
  assert(snprintf(moved_stage,sizeof(moved_stage),
                  "%s/.POSTSAVE.original",backup)>0);
  replace_stage=1;post_sync_swap=1;
  assert(!cmp5_backup_media(root,&item,backup,"POSTSAVE",&receipt,NULL,NULL));
  assert(stage_replaced&&!receipt.published);
  assert(access(watched_stage,F_OK)==0&&access(moved_stage,F_OK)==0);
  assert(access(moved_stage,F_OK)==0);
  struct stat published;
  assert(fstatat(AT_FDCWD,watched_stage,&published,AT_SYMLINK_NOFOLLOW)==0);
  assert(published.st_size==(off_t)(sizeof(stage_sentinel)-1u));
 }else if(!strcmp(action,"restore-stage-swapped")){
  assert(snprintf(watched_stage,sizeof(watched_stage),
                  "%s/.RESTAGE.part",restore)>0);
  assert(snprintf(moved_stage,sizeof(moved_stage),
                  "%s/.RESTAGE.original",restore)>0);
  replace_stage=1;
  assert(!cmp5_restore_backup(backup,"GOOD",restore,"RESTAGE",
                              &receipt,NULL,NULL));
  assert(stage_replaced&&!receipt.published);
  assert(access(watched_stage,F_OK)==0&&access(moved_stage,F_OK)==0);
 }else if(!strcmp(action,"restore-stage-postfsync-swapped")){
  assert(snprintf(watched_stage,sizeof(watched_stage),
                  "%s/.POSTRESTORE.part",restore)>0);
  assert(snprintf(moved_stage,sizeof(moved_stage),
                  "%s/.POSTRESTORE.original",restore)>0);
  replace_stage=1;post_sync_swap=1;
  assert(!cmp5_restore_backup(backup,"GOOD",restore,"POSTRESTORE",
                              &receipt,NULL,NULL));
  assert(stage_replaced&&!receipt.published);
  assert(access(watched_stage,F_OK)==0&&access(moved_stage,F_OK)==0);
 }else if(!strcmp(action,"backup-publish-tamper")){
  corrupt_published_restore=1;
  assert(!cmp5_backup_media(root,&item,backup,"BROKEN",&receipt,
    NULL,NULL));
  assert(!receipt.published);
 }else if(!strcmp(action,"backup-rename")){
  (void)snprintf(watching,sizeof(watching),"%s",original);
  (void)snprintf(moved,sizeof(moved),"%s",new_path);
  armed=1;
  assert(!cmp5_backup_media(root,&item,backup,"RACED",&receipt,NULL,NULL));
  assert(triggered&&!receipt.published);
 }else if(!strcmp(action,"verify-rename")){
  (void)snprintf(watching,sizeof(watching),"%s/GOOD",backup);
  (void)snprintf(moved,sizeof(moved),"%s",new_path);
  armed=1;
  assert(!cmp5_verify_backup(backup,"GOOD",&receipt,NULL,NULL));
  assert(triggered&&!receipt.published);
 }else if(!strcmp(action,"verify-workspace-swap")){
  SwapWorkspace state={backup,new_path,0u,0};
  assert(!cmp5_verify_backup(backup,"GOOD",&receipt,
    change_workspace_during_read,&state));
  assert(state.changed&&state.calls>=2u&&!receipt.published);
 }else if(!strcmp(action,"restore-rename")){
  (void)snprintf(watching,sizeof(watching),"%s/GOOD",backup);
  (void)snprintf(moved,sizeof(moved),"%s",new_path);
  restore_only=1;
  assert(!cmp5_restore_backup(backup,"GOOD",restore,"RENAMED",&receipt,NULL,NULL));
  assert(triggered&&!receipt.published);
 }else if(!strcmp(action,"restore-backup-workspace-swap")){
  restore_only=1;
  SwapWorkspace state={backup,new_path,0u,0};
  assert(!cmp5_restore_backup(backup,"GOOD",restore,"BACKUPSWAP",&receipt,
    change_workspace_during_restore,&state));
  assert(state.changed&&!receipt.published);
 }else if(!strcmp(action,"restore-target-workspace-swap")){
  restore_only=1;
  SwapWorkspace state={restore,new_path,0u,0};
  assert(!cmp5_restore_backup(backup,"GOOD",restore,"TARGETSWAP",&receipt,
    change_workspace_during_restore,&state));
  assert(state.changed&&!receipt.published);
 }else if(!strcmp(action,"restore-publish-tamper")){
  corrupt_published_restore=1;
  assert(!cmp5_restore_backup(backup,"GOOD",restore,"CORRUPT",
    &receipt,NULL,NULL));
  assert(!receipt.published);
 }else if(!strcmp(action,"same-workspace")){
  assert(!cmp5_restore_backup(backup,"GOOD",backup,"SAVED",&receipt,NULL,NULL));
  assert(!cmp5_restore_backup(backup,"GOOD",new_path,"SAVED",&receipt,NULL,NULL));
 }else if(!strcmp(action,"fsync-backup")){
  fail_directory_fsync=1;
  assert(!cmp5_backup_media(root,&item,backup,"UNSYNCED",&receipt,NULL,NULL));
  fail_directory_fsync=0;
  assert(!receipt.published);
  /* Publication may have occurred before the directory reported failure;
   * never auto-delete the committed file or claim it is durable. */
  Cmp5BackupReceipt probe={0};
  assert(cmp5_verify_backup(backup,"UNSYNCED",&probe,NULL,NULL));
 }else if(!strcmp(action,"fsync-restore")){
  fail_directory_fsync=1;
  assert(!cmp5_restore_backup(backup,"GOOD",restore,"UNSYNCED",&receipt,NULL,NULL));
  fail_directory_fsync=0;
  assert(!receipt.published);
 }else return 3;
 puts("Private backup name-integrity race rejected: PASS");
 return 0;
}
