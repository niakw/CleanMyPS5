/* Two independent, valid groups in one private workspace.
 * Interprocess contention must fail closed for backup and restore.
 * Never uses real gallery files or PS5 writes. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "verified_group.h"
#include "media_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
typedef struct {
 char path[1024];
 int signal_fd,release_fd,announced;
} Pause;
static int pause_after_first_member(void*context){
 Pause*p=(Pause*)context;
 if(!p->announced&&access(p->path,F_OK)==0){
  p->announced=1;
  const char marker='L';
  if(write(p->signal_fd,&marker,1)!=1)_exit(70);
  char release=0;
  if(read(p->release_fd,&release,1)!=1||release!='R')_exit(71);
 }
 return 0;
}
static void wait_locked(int fd){
 char marker=0;
 assert(read(fd,&marker,1)==1&&marker=='L');
}
static void release_locked(int fd){
 assert(write(fd,"R",1)==1);
}
static void join_child(pid_t pid){
 int status=0;
 assert(waitpid(pid,&status,0)==pid);
 assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
}
static void snapshot(Cmp5MediaCandidate*c,const char*path,
 const char*relative,unsigned type){
 struct stat st={0};assert(stat(path,&st)==0);
 memset(c,0,sizeof(*c));
 c->bytes=(uint64_t)st.st_size;
 c->media_type=type;
 assert(strlen(relative)<sizeof(c->preview_path));
 strcpy(c->preview_path,relative);
 cmp5_snapshot_capture(c,&st);
}
static pid_t spawn(int ready[2],int release[2]){
 assert(pipe(ready)==0&&pipe(release)==0);
 pid_t pid=fork();
 assert(pid>=0);return pid;
}
int main(int argc,char**argv){
 if(argc!=6)return 2;
 const char*root=argv[1],*jpg=argv[2],*dat=argv[3],
   *backup=argv[4],*restore=argv[5];
 Cmp5MediaCandidate source[2];
 snapshot(&source[0],jpg,
  "/user/av_contents/photo/NPXS00000/GAME123/REC_001.jpg",1u);
 snapshot(&source[1],dat,
  "/user/av_contents/photo/NPXS00000/GAME123/REC_001.dat",3u);
 assert(cmp5_validate_capture_group(source,2u));
 Cmp5GroupReceipt receipt={0};
 int ready[2],release[2];
 pid_t child=spawn(ready,release);
 if(child==0){
  close(ready[0]);close(release[1]);
  Pause pause={0};
  assert(snprintf(pause.path,sizeof(pause.path),
    "%s/BACKA__1",backup)>0);
  pause.signal_fd=ready[1];pause.release_fd=release[0];
  const int ok=cmp5_backup_group(root,source,2u,backup,"BACKA",
    &receipt,pause_after_first_member,&pause);
  _exit(ok&&pause.announced?0:72);
 }
 close(ready[1]);close(release[0]);wait_locked(ready[0]);
 Cmp5GroupReceipt denied={0};
 assert(!cmp5_backup_group(root,source,2u,backup,"BACKB",
   &denied,NULL,NULL));
 assert(denied.count==0u);
 char unexpected[1024];
 snprintf(unexpected,sizeof(unexpected),"%s/BACKB__1",backup);
 assert(access(unexpected,F_OK)!=0);
 release_locked(release[1]);close(ready[0]);close(release[1]);
 join_child(child);
 assert(cmp5_verify_group(backup,"BACKA",&receipt,NULL,NULL));
 assert(receipt.count==2u);
 assert(cmp5_backup_group(root,source,2u,backup,"BACKB",
   &receipt,NULL,NULL));
 assert(cmp5_verify_group(backup,"BACKB",&receipt,NULL,NULL));

 child=spawn(ready,release);
 if(child==0){
  close(ready[0]);close(release[1]);
  Pause pause={0};
  assert(snprintf(pause.path,sizeof(pause.path),
    "%s/BACKA__1",restore)>0);
  pause.signal_fd=ready[1];pause.release_fd=release[0];
  const int ok=cmp5_restore_group(backup,"BACKA",restore,
    &receipt,pause_after_first_member,&pause);
  _exit(ok&&pause.announced?0:73);
 }
 close(ready[1]);close(release[0]);wait_locked(ready[0]);
 assert(!cmp5_restore_group(backup,"BACKB",restore,
    &denied,NULL,NULL));
 assert(denied.count==0u);
 /* A writer must not publish into the SH-locked source during restore. */
 assert(!cmp5_backup_group(root,source,2u,backup,"BACKC",
    &denied,NULL,NULL));
 snprintf(unexpected,sizeof(unexpected),"%s/BACKC__1",backup);
 assert(access(unexpected,F_OK)!=0);
 release_locked(release[1]);close(ready[0]);close(release[1]);
 join_child(child);
 assert(cmp5_verify_restored_group(restore,"BACKA",&receipt,NULL,NULL));
 assert(receipt.count==2u);
 assert(cmp5_restore_group(backup,"BACKB",restore,
   &receipt,NULL,NULL));
 assert(cmp5_verify_restored_group(restore,"BACKB",&receipt,NULL,NULL));
 assert(cmp5_backup_group(root,source,2u,backup,"BACKC",
   &receipt,NULL,NULL));
 assert(cmp5_verify_group(backup,"BACKC",&receipt,NULL,NULL));
 puts("Interprocess group backup/restore locks: contention and release PASS");
 return 0;
}
