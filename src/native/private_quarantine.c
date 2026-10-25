#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "private_quarantine.h"
#include "private_workspace_guard.h"
#include "verified_backup.h"
#include "sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

#define LAB_NAME "cmp5-quarantine-lab"
#define CHUNK 65536u

static int valid_id(const char*name){
 if(!name)return 0;
 size_t n=0u;
 for(;n<24u&&name[n];n++){
  const char c=name[n];
  if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return 0;
 }
 return n>0u&&n<24u&&name[n]=='\0';
}
static void held_name(char*out,const char*name){
 (void)snprintf(out,48,".%s.CMP5_HELD",name);
}
static int open_lab(const char*path){
 if(!path||!cmp5_workspace_name_ok(path))return -1;
 const char*last=strrchr(path,'/');
 if(!last||strcmp(last+1,LAB_NAME)!=0)return -1;
 return cmp5_open_private_workspace(path);
}
/* All mutations within one private lab are serialized across cooperating
 * processes. The directory inode itself is the lock identity; no extra
 * lock files, symlink names or cleanup operations are needed. Fail closed
 * when the filesystem does not provide advisory directory locks. This
 * never protects against third-party processes that ignore advisory locks. */
static int lock_lab(int dir){
 return dir>=0&&flock(dir,LOCK_EX|LOCK_NB)==0;
}
static int state_entry(int dir,const char*name){
 struct stat st={0};
 if(fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW)<0)
  return errno==ENOENT?0:-1;
 /* Neither symbolic links nor aliases nor public files belong to a lab. */
 return S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&
   (st.st_mode&077)==0&&st.st_nlink==1&&
   st.st_size>0&&(uint64_t)st.st_size<=CMP5_BACKUP_LIMIT?1:-1;
}
static Cmp5LabState state_at(int dir,const char*item){
 char held[48];held_name(held,item);
 const int source=state_entry(dir,item),quarantined=state_entry(dir,held);
 if(source<0||quarantined<0)return CMP5_LAB_AMBIGUOUS;
 if(source&&quarantined)return CMP5_LAB_AMBIGUOUS;
 if(source)return CMP5_LAB_SOURCE_ONLY;
 if(quarantined)return CMP5_LAB_HELD_ONLY;
 return CMP5_LAB_EMPTY;
}
Cmp5LabState cmp5_private_lab_state(const char*lab,const char*item){
 if(!valid_id(item))return CMP5_LAB_INVALID;
 int dir=open_lab(lab);
 if(dir<0)return CMP5_LAB_INVALID;
 const Cmp5LabState state=state_at(dir,item);
 close(dir);return state;
}
static int same_snapshot(const struct stat*a,const struct stat*b){
#if defined(__APPLE__)
 const int nanos=a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec&&
   a->st_ctimespec.tv_nsec==b->st_ctimespec.tv_nsec;
#else
 const int nanos=a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
   a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
#endif
 return nanos&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
   a->st_size==b->st_size&&a->st_mode==b->st_mode&&
   a->st_mtime==b->st_mtime&&a->st_ctime==b->st_ctime&&
   a->st_nlink==b->st_nlink;
}
static int same_inode(const struct stat*a,const struct stat*b){
 return S_ISREG(b->st_mode)&&a->st_dev==b->st_dev&&
   a->st_ino==b->st_ino&&a->st_size==b->st_size&&
   b->st_uid==geteuid()&&(b->st_mode&077)==0;
}
static int dir_current(int dir,const char*path){
 int next=cmp5_open_private_workspace(path);
 if(next<0)return 0;
 struct stat old={0},now={0};
 const int ok=fstat(dir,&old)==0&&fstat(next,&now)==0&&
   S_ISDIR(now.st_mode)&&old.st_dev==now.st_dev&&old.st_ino==now.st_ino;
 close(next);return ok;
}
static int hash_file_at(int dir,const char*name,
 const Cmp5BackupReceipt*receipt,struct stat*identity,unsigned links){
 if(!receipt||!receipt->published||!receipt->bytes)return 0;
 int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 if(fd<0)return 0;
 struct stat before={0},after={0},named={0};
 int ok=fstat(fd,&before)==0&&S_ISREG(before.st_mode)&&
   before.st_uid==geteuid()&&(before.st_mode&077)==0&&
   before.st_nlink==(nlink_t)links&&before.st_size>0&&
   (uint64_t)before.st_size==receipt->bytes;
 Cmp5Sha256 sha;cmp5_sha256_init(&sha);
 unsigned char buffer[CHUNK];
 for(uint64_t done=0;ok&&done<receipt->bytes;){
  const size_t wanted=(size_t)(receipt->bytes-done>sizeof(buffer)?
    sizeof(buffer):receipt->bytes-done);
  ssize_t n=read(fd,buffer,wanted);
  if(n<0&&errno==EINTR)continue;
  if(n<=0){ok=0;break;}
  cmp5_sha256_update(&sha,buffer,(size_t)n);
  done+=(uint64_t)n;
 }
 unsigned char digest[32]={0};
 if(ok)cmp5_sha256_final(&sha,digest);
 if(ok)ok=memcmp(digest,receipt->sha256,sizeof(digest))==0&&
   fstat(fd,&after)==0&&
   fstatat(dir,name,&named,AT_SYMLINK_NOFOLLOW)==0&&
   same_snapshot(&before,&after)&&same_snapshot(&before,&named);
 close(fd);
 if(ok&&identity)*identity=before;
 return ok;
}
static int distinct(int left,int right){
 struct stat a={0},b={0};
 return fstat(left,&a)==0&&fstat(right,&b)==0&&
   (a.st_dev!=b.st_dev||a.st_ino!=b.st_ino);
}
int cmp5_private_lab_hold_checkpoint(const char*lab,const char*item,
 const char*backup,const char*backup_id,
 const char*rehearsal,const char*rehearsal_id,
 Cmp5LabCheckpoint checkpoint,void*context){
 if(!valid_id(item)||!valid_id(rehearsal_id)||
    !lab||!backup||!rehearsal||!backup_id)return 0;
 int source=open_lab(lab);
 if(source<0)return 0;
 if(!lock_lab(source)){close(source);return 0;}
 int copies=cmp5_open_private_workspace(backup);
 int test=cmp5_open_private_workspace(rehearsal);
 int ok=copies>=0&&test>=0&&
   distinct(source,copies)&&distinct(source,test)&&
   distinct(copies,test)&&state_at(source,item)==CMP5_LAB_SOURCE_ONLY;
 if(copies>=0)close(copies);
 if(test>=0)close(test);
 Cmp5BackupReceipt saved={0},trial={0};
 if(ok)ok=cmp5_verify_backup(backup,backup_id,&saved,NULL,NULL)&&
   hash_file_at(source,item,&saved,NULL,1u);
 /* Rehearse actual content restoration before touching the lab source.
  * The separate private rehearsal is never the PS5 Gallery. */
 if(ok)ok=cmp5_restore_backup(backup,backup_id,rehearsal,rehearsal_id,
   &trial,NULL,NULL)&&trial.published&&trial.bytes==saved.bytes&&
   memcmp(trial.sha256,saved.sha256,32)==0;
 int rehearsal_fd=-1;
 if(ok)rehearsal_fd=cmp5_open_private_workspace(rehearsal);
 if(ok)ok=rehearsal_fd>=0&&
   hash_file_at(rehearsal_fd,rehearsal_id,&saved,NULL,1u);
 if(rehearsal_fd>=0)close(rehearsal_fd);
 struct stat before={0},created={0},named={0};
 if(ok)ok=cmp5_verify_backup(backup,backup_id,NULL,NULL,NULL)&&
   state_at(source,item)==CMP5_LAB_SOURCE_ONLY&&
   hash_file_at(source,item,&saved,&before,1u)&&dir_current(source,lab);
 char held[48];held_name(held,item);
 if(ok)ok=linkat(source,item,source,held,0)==0;
 /* If an interruption happens between link and unlink, BOTH names
  * remain and recovery refuses automatic guesses or destructive repairs. */
 if(ok)ok=fstatat(source,held,&created,AT_SYMLINK_NOFOLLOW)==0&&
   fstatat(source,item,&named,AT_SYMLINK_NOFOLLOW)==0&&
   same_inode(&before,&created)&&same_inode(&before,&named)&&
   created.st_nlink==2&&named.st_nlink==2&&
   dir_current(source,lab)&&fsync(source)==0;
 if(ok&&checkpoint&&checkpoint(context))ok=0;
 /* The two-name checkpoint is durable, but the original bytes can still
  * change while a callback or an uncooperative process runs. Before
  * removing the visible SOURCE name, hash both links against the verified
  * independent capsule and require that they STILL share one inode.
  * If they do not, retain BOTH names for explicit offline inspection. */
 if(ok){
  struct stat live_source={0},live_held={0};
  ok=hash_file_at(source,item,&saved,&live_source,2u)&&
     hash_file_at(source,held,&saved,&live_held,2u)&&
     same_inode(&live_source,&live_held)&&
     live_source.st_nlink==2&&live_held.st_nlink==2&&
     state_at(source,item)==CMP5_LAB_AMBIGUOUS&&
     dir_current(source,lab);
 }
 if(ok)ok=unlinkat(source,item,0)==0&&fsync(source)==0&&
   state_at(source,item)==CMP5_LAB_HELD_ONLY&&
   hash_file_at(source,held,&saved,NULL,1u)&&dir_current(source,lab);
 close(source);return ok;
}
int cmp5_private_lab_recover_checkpoint(const char*lab,const char*item,
 const char*backup,const char*backup_id,
 Cmp5LabCheckpoint checkpoint,void*context){
 if(!valid_id(item)||!lab||!backup||!backup_id)return 0;
 int dir=open_lab(lab);
 if(dir<0)return 0;
 if(!lock_lab(dir)){close(dir);return 0;}
 int backup_dir=cmp5_open_private_workspace(backup);
 int ok=backup_dir>=0&&distinct(dir,backup_dir)&&
   state_at(dir,item)==CMP5_LAB_HELD_ONLY;
 if(backup_dir>=0)close(backup_dir);
 Cmp5BackupReceipt saved={0};
 if(ok)ok=cmp5_verify_backup(backup,backup_id,&saved,NULL,NULL);
 char held[48];held_name(held,item);
 struct stat before={0},created={0},named={0};
 if(ok)ok=hash_file_at(dir,held,&saved,&before,1u)&&dir_current(dir,lab);
 if(ok)ok=linkat(dir,held,dir,item,0)==0;
 if(ok)ok=fstatat(dir,item,&created,AT_SYMLINK_NOFOLLOW)==0&&
   fstatat(dir,held,&named,AT_SYMLINK_NOFOLLOW)==0&&
   same_inode(&before,&created)&&same_inode(&before,&named)&&
   created.st_nlink==2&&named.st_nlink==2&&
   dir_current(dir,lab)&&fsync(dir)==0;
 if(ok&&checkpoint&&checkpoint(context))ok=0;
 /* Confirm BOTH hardlink names still hash to the original backup before
  * dropping the held name. A changed payload leaves the two-name
  * checkpoint intact rather than claiming recovery succeeded. */
 if(ok){
  struct stat live_source={0},live_held={0};
  ok=hash_file_at(dir,item,&saved,&live_source,2u)&&
     hash_file_at(dir,held,&saved,&live_held,2u)&&
     same_inode(&live_source,&live_held)&&
     live_source.st_nlink==2&&live_held.st_nlink==2&&
     state_at(dir,item)==CMP5_LAB_AMBIGUOUS&&
     dir_current(dir,lab);
 }
 if(ok)ok=unlinkat(dir,held,0)==0&&fsync(dir)==0&&
   state_at(dir,item)==CMP5_LAB_SOURCE_ONLY&&
   hash_file_at(dir,item,&saved,NULL,1u)&&dir_current(dir,lab);
 close(dir);return ok;
}

// Normal public entrypoints require the same independent evidence but no
// synthetic test checkpoint. They never resolve double-linked states.
int cmp5_private_lab_hold(const char*lab,const char*item,
 const char*backup,const char*backup_id,
 const char*rehearsal,const char*rehearsal_id){
 return cmp5_private_lab_hold_checkpoint(lab,item,backup,backup_id,
   rehearsal,rehearsal_id,NULL,NULL);
}
int cmp5_private_lab_recover(const char*lab,const char*item,
 const char*backup,const char*backup_id){
 return cmp5_private_lab_recover_checkpoint(lab,item,backup,backup_id,
   NULL,NULL);
}
/* A manual, explicitly directed transition resolution is permitted ONLY
 * for two names on one same regular, private inode with EXACTLY two links.
 * No regular alias, symlink or second inode can be "recovered" this way.
 * It cannot be triggered by the read-only scanners or a PS5 UI event. */
int cmp5_private_lab_resolve_interrupted(const char*lab,const char*item,
 const char*backup,const char*backup_id,Cmp5LabResolution resolution){
 if(!valid_id(item)||(resolution!=CMP5_LAB_KEEP_SOURCE&&
     resolution!=CMP5_LAB_KEEP_HELD)||!lab||!backup||!backup_id)return 0;
 int dir=open_lab(lab);
 if(dir<0)return 0;
 if(!lock_lab(dir)){close(dir);return 0;}
 int copies=cmp5_open_private_workspace(backup);
 int ok=copies>=0&&distinct(dir,copies)&&
   state_at(dir,item)==CMP5_LAB_AMBIGUOUS;
 if(copies>=0)close(copies);
 Cmp5BackupReceipt saved={0};
 if(ok)ok=cmp5_verify_backup(backup,backup_id,&saved,NULL,NULL);
 char held[48];held_name(held,item);
 struct stat initial={0},other={0};
 if(ok)ok=hash_file_at(dir,item,&saved,&initial,2u)&&
   hash_file_at(dir,held,&saved,&other,2u)&&
   same_inode(&initial,&other)&&
   initial.st_nlink==2&&other.st_nlink==2&&
   same_snapshot(&initial,&other)&&
   dir_current(dir,lab);
 /* No mutation until the user-requested destination has been proved,
  * both filenames are still identical and metadata is durable. */
 if(ok)ok=fsync(dir)==0;
 if(ok){
  const char*remove_name=resolution==CMP5_LAB_KEEP_SOURCE?held:item;
  ok=unlinkat(dir,remove_name,0)==0;
 }
 if(ok)ok=fsync(dir)==0&&dir_current(dir,lab)&&
   state_at(dir,item)==(resolution==CMP5_LAB_KEEP_SOURCE?
     CMP5_LAB_SOURCE_ONLY:CMP5_LAB_HELD_ONLY)&&
   hash_file_at(dir,resolution==CMP5_LAB_KEEP_SOURCE?item:held,
     &saved,NULL,1u);
 close(dir);
 return ok;
}
