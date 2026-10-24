/* Private laboratory rehearsal only. No system or Gallery writes. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "private_quarantine.h"
#include "verified_backup.h"
#include "media_snapshot.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
/* Forked worker holds its laboratory lock across the two-name
 * transition. A second process must fail on another VALID item, proving
 * that the rejection is contention rather than an invalid source. */
static int announced_fd=-1,release_fd=-1;
static int block_at_checkpoint(void*unused){
 (void)unused;
 const char mark='L';
 if(write(announced_fd,&mark,1)!=1)_exit(90);
 char release=0;
 if(read(release_fd,&release,1)!=1||release!='R')_exit(91);
 return 0;
}
static int simulate_abrupt_exit(void*unused){
 (void)unused;
 _exit(77); /* hard process exit after fsync of two-name checkpoint */
}
static void expect_child_crash(pid_t pid){
 assert(pid>0);
 int status=0;
 assert(waitpid(pid,&status,0)==pid);
 assert(WIFEXITED(status)&&WEXITSTATUS(status)==77);
}

static void equal(const char*a,const char*b){
 FILE*left=fopen(a,"rb"),*right=fopen(b,"rb");
 assert(left&&right);
 unsigned char x[1000],y[1000];
 for(;;){
  size_t n=fread(x,1,sizeof(x),left);
  size_t m=fread(y,1,sizeof(y),right);
  assert(n==m&&memcmp(x,y,n)==0);
  if(!n)break;
 }
 assert(fclose(left)==0&&fclose(right)==0);
}
int main(int argc,char**argv){
 if(argc!=7)return 2;
 const char*root=argv[1],*gallery=argv[2],*lab=argv[3],
   *backup=argv[4],*trial=argv[5],*unsafe=argv[6];
 Cmp5MediaCandidate snapshot={0};
 struct stat st={0};
 assert(stat(gallery,&st)==0);
 snapshot.media_type=1u;snapshot.bytes=(uint64_t)st.st_size;
 strcpy(snapshot.preview_path,
   "/user/av_contents/photo/NPXS00000/GAME123/DEMO.JPG");
 cmp5_snapshot_capture(&snapshot,&st);
 Cmp5BackupReceipt receipt={0};
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_SOURCE_ONLY);
 assert(cmp5_private_lab_state(unsafe,"ITEM")==CMP5_LAB_INVALID);
 assert(!cmp5_private_lab_hold(lab,"ITEM",backup,"ONE",trial,"TRIAL"));
 assert(cmp5_backup_media(root,&snapshot,backup,"ONE",&receipt,NULL,NULL));
 assert(receipt.published&&receipt.bytes>0);
 assert(!cmp5_private_lab_hold(unsafe,"ITEM",backup,"ONE",trial,"TRIAL"));
 assert(!cmp5_private_lab_hold(lab,"../ITEM",backup,"ONE",trial,"TRIAL"));
 assert(cmp5_private_lab_hold(lab,"ITEM",backup,"ONE",trial,"TRIAL"));
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_HELD_ONLY);
 char held[1024],src[1024],rehearsed[1024];
 snprintf(src,sizeof(src),"%s/ITEM",lab);
 snprintf(held,sizeof(held),"%s/.ITEM.CMP5_HELD",lab);
 snprintf(rehearsed,sizeof(rehearsed),"%s/TRIAL",trial);
 assert(access(src,F_OK)!=0&&access(held,F_OK)==0);
 equal(gallery,held);equal(gallery,rehearsed);
 assert(!cmp5_private_lab_hold(lab,"ITEM",backup,"ONE",trial,"TRIAL"));
 assert(cmp5_private_lab_recover(lab,"ITEM",backup,"ONE"));
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_SOURCE_ONLY);
 assert(access(held,F_OK)!=0);equal(gallery,src);
 assert(!cmp5_private_lab_recover(lab,"ITEM",backup,"ONE"));
 /* A hard exit after the durable two-name checkpoint must not silently
  * pick a winner. Explicitly resolve only after verifying two equal
  * hardlinks against the original independent SHA-256 capsule. */
 pid_t child=fork();
 assert(child>=0);
 if(child==0){
  (void)cmp5_private_lab_hold_checkpoint(lab,"ITEM",backup,"ONE",
    trial,"TRIAL2",simulate_abrupt_exit,NULL);
  _exit(79);
 }
 expect_child_crash(child);
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_AMBIGUOUS);
 assert(!cmp5_private_lab_hold(lab,"ITEM",backup,"ONE",trial,"TRIAL3"));
 assert(!cmp5_private_lab_recover(lab,"ITEM",backup,"ONE"));
 assert(!cmp5_private_lab_resolve_interrupted(lab,"ITEM",backup,
   "ONE",(Cmp5LabResolution)0));
 assert(cmp5_private_lab_resolve_interrupted(lab,"ITEM",backup,
   "ONE",CMP5_LAB_KEEP_SOURCE));
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_SOURCE_ONLY);
 assert(!cmp5_private_lab_resolve_interrupted(lab,"ITEM",backup,
   "ONE",CMP5_LAB_KEEP_HELD));
 /* Simulate a second crash, this time during restoration back to source. */
 assert(cmp5_private_lab_hold(lab,"ITEM",backup,"ONE",trial,"TRIAL3"));
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_HELD_ONLY);
 child=fork();
 assert(child>=0);
 if(child==0){
  (void)cmp5_private_lab_recover_checkpoint(lab,"ITEM",backup,"ONE",
    simulate_abrupt_exit,NULL);
  _exit(79);
 }
 expect_child_crash(child);
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_AMBIGUOUS);
 assert(cmp5_private_lab_resolve_interrupted(lab,"ITEM",backup,
   "ONE",CMP5_LAB_KEEP_HELD));
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_HELD_ONLY);
 assert(cmp5_private_lab_recover(lab,"ITEM",backup,"ONE"));
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_SOURCE_ONLY);
 equal(gallery,src);
 /* Verify serialization of the GENERAL transaction mechanism with a
  * second independently valid item, not a broken duplicate candidate. */
 char second[1024];
 snprintf(second,sizeof(second),"%s/ITEM2",lab);
 int item2=open(second,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
 assert(item2>=0);
 FILE*payload=fopen(gallery,"rb");
 assert(payload);
 unsigned char block[1024];
 for(;;){
  size_t n=fread(block,1,sizeof(block),payload);
  if(n==0)break;
  assert(write(item2,block,n)==(ssize_t)n);
 }
 assert(fclose(payload)==0&&close(item2)==0);
 assert(cmp5_private_lab_state(lab,"ITEM2")==CMP5_LAB_SOURCE_ONLY);
 int ready[2],release[2];
 assert(pipe(ready)==0&&pipe(release)==0);
 child=fork();
 assert(child>=0);
 if(child==0){
  close(ready[0]);close(release[1]);
  announced_fd=ready[1];release_fd=release[0];
  const int held_ok=cmp5_private_lab_hold_checkpoint(
    lab,"ITEM",backup,"ONE",trial,"LOCKED",block_at_checkpoint,NULL);
  _exit(held_ok?0:92);
 }
 close(ready[1]);close(release[0]);
 char marker=0;
 assert(read(ready[0],&marker,1)==1&&marker=='L');
 /* Second item has a matching verified capsule and distinct rehearsal
  * output; only the interprocess lock can cause this denial. */
 assert(!cmp5_private_lab_hold(lab,"ITEM2",backup,"ONE",trial,"TRY2"));
 assert(cmp5_private_lab_state(lab,"ITEM2")==CMP5_LAB_SOURCE_ONLY);
 assert(write(release[1],"R",1)==1);
 close(ready[0]);close(release[1]);
 int complete=0;
 assert(waitpid(child,&complete,0)==child);
 assert(WIFEXITED(complete)&&WEXITSTATUS(complete)==0);
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_HELD_ONLY);
 assert(cmp5_private_lab_hold(lab,"ITEM2",backup,"ONE",trial,"TRY3"));
 assert(cmp5_private_lab_state(lab,"ITEM2")==CMP5_LAB_HELD_ONLY);
 assert(cmp5_private_lab_recover(lab,"ITEM2",backup,"ONE"));
 assert(cmp5_private_lab_state(lab,"ITEM2")==CMP5_LAB_SOURCE_ONLY);
 assert(cmp5_private_lab_recover(lab,"ITEM",backup,"ONE"));
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_SOURCE_ONLY);
 equal(gallery,src);
 /* A changed source is not eligible to be quarantined just because
  * an old backup exists. */
 int altered=open(src,O_WRONLY|O_NOFOLLOW);
 assert(altered>=0);
 assert(write(altered,"X",1)==1);
 assert(close(altered)==0);
 assert(!cmp5_private_lab_hold(lab,"ITEM",backup,"ONE",trial,"SECOND"));
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_SOURCE_ONLY);
 /* No clobber, ever: a symlink masquerading as a held member causes
  * ambiguous state, so neither the source nor the symlink is changed. */
 char attacker[1024];
 snprintf(attacker,sizeof(attacker),"%s/.ITEM.CMP5_HELD",lab);
 assert(symlink(gallery,attacker)==0);
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_AMBIGUOUS);
 assert(!cmp5_private_lab_recover(lab,"ITEM",backup,"ONE"));
 assert(!cmp5_private_lab_hold(lab,"ITEM",backup,"ONE",trial,"SECOND"));
 assert(unlink(attacker)==0);
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_SOURCE_ONLY);
 puts("Private lab backup, rehearsal, quarantine, recovery, tamper & alias: PASS");
 return 0;
}
