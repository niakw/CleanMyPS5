#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "verified_group.h"
#include "sha256.h"
#include "private_workspace_guard.h"
#include "media_snapshot.h"
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define MARK_SIZE 256u
#define BODY_DIGEST_OFFSET 184u
/* Private-only mapping of original Gallery locators to ordered capsules.
 * Four fixed records: pathname[256], bytes[8], media_type[4]. */
#define ORIGIN_RECORD_SIZE 268u
#define ORIGIN_HEADER_SIZE 44u
#define ORIGIN_DIGEST_OFFSET (ORIGIN_HEADER_SIZE+CMP5_GROUP_MAX*ORIGIN_RECORD_SIZE)
#define ORIGIN_SIZE (ORIGIN_DIGEST_OFFSET+32u)
#define MAX_TOTAL (512ULL*1024ULL*1024ULL)
#define BLOCK 65536u
static const uint8_t group_magic[8]={'C','M','P','5','G','R','P','2'};
static const uint8_t origin_magic[8]={'C','M','P','5','M','A','P','1'};
static int cancelled(Cmp5CancelCheck check,void*ctx){return check&&check(ctx);}
static void u32(uint8_t*p,uint32_t n){
 p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);
 p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;
}
static uint32_t g32(const uint8_t*p){
 return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
  ((uint32_t)p[2]<<8)|p[3];
}
static void u64(uint8_t*p,uint64_t n){
 for(unsigned i=0;i<8;i++)p[7-i]=(uint8_t)(n>>(8*i));
}
static uint64_t g64(const uint8_t*p){
 uint64_t n=0;for(unsigned i=0;i<8;i++)n=(n<<8)|p[i];return n;
}
static void hash(const void*data,size_t n,uint8_t result[32]){
 Cmp5Sha256 h;cmp5_sha256_init(&h);
 cmp5_sha256_update(&h,data,n);cmp5_sha256_final(&h,result);
}
static int valid_id(const char*id){
 if(!id)return 0;
 size_t i=0;
 /* A 31-byte field with no NUL is invalid; never probe id[31]. */
 for(;i<31u;i++){
  const char c=id[i];
  if(c=='\0')return i>0u;
  if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return 0;
 }
 return 0;
}
static void member_name(char out[48],const char*id,unsigned index){
 (void)snprintf(out,48,"%s__%u",id,index+1u);
}
static void marker_name(char out[48],const char*id,int restored){
 (void)snprintf(out,48,"%s__%s",id,restored?"RESTORED":"COMMIT");
}
static void origin_name(char out[48],const char*id){
 (void)snprintf(out,48,"%s__ORIGINS",id);
}
static int safe_dir(const char*path){
 return cmp5_open_private_workspace(path);
}
static int same_file(const struct stat*a,const struct stat*b){
#if defined(__APPLE__)
 const int nsec=a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec&&
   a->st_ctimespec.tv_nsec==b->st_ctimespec.tv_nsec;
#else
 const int nsec=a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
   a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
#endif
 return nsec&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
  a->st_size==b->st_size&&a->st_mtime==b->st_mtime&&
  a->st_ctime==b->st_ctime&&a->st_mode==b->st_mode&&
  a->st_nlink==b->st_nlink;
}
/* A descriptor survives rename/unlink of its pathname. Do not report a
 * verified group for bytes in an orphaned directory when the caller's
 * selected private workspace now names another one (or no directory). */
static int workspace_unchanged(int dir,const char*path,
                               const struct stat*initial){
 struct stat anchored,current,named;
 if(fstat(dir,&anchored)!=0||!same_file(initial,&anchored))return 0;
 int reopened=safe_dir(path);
 if(reopened<0)return 0;
 const int ok=fstat(reopened,&current)==0&&
   same_file(initial,&current)&&
   anchored.st_dev==current.st_dev&&anchored.st_ino==current.st_ino;
 close(reopened);
 if(!ok)return 0;
 /* The original fd is the trusted anchor, not a stale pathname. */
 return fstat(dir,&named)==0&&same_file(initial,&named);
}
static int read_exact(int fd,uint8_t*p,size_t n){
 while(n){ssize_t r=read(fd,p,n);
  if(r<0&&errno==EINTR)continue;
  if(r<=0)return 0;
  p+=r;n-=(size_t)r;
 }return 1;
}
static int write_exact(int fd,const uint8_t*p,size_t n){
 while(n){ssize_t r=write(fd,p,n);
  if(r<0&&errno==EINTR)continue;
  if(r<=0)return 0;
  p+=r;n-=(size_t)r;
 }return 1;
}
static void manifest_header(uint8_t out[MARK_SIZE],const Cmp5BackupReceipt*r,
 uint32_t count){
 memset(out,0,MARK_SIZE);
 memcpy(out,group_magic,8);
 u32(out+8,count);
 uint64_t total=0;
 for(unsigned i=0;i<count;i++){
  u64(out+24u+i*40u,r[i].bytes);
  memcpy(out+24u+i*40u+8u,r[i].sha256,32);
  total+=r[i].bytes;
 }
 u64(out+16,total);
 hash(out,BODY_DIGEST_OFFSET,out+BODY_DIGEST_OFFSET);
}
static int parse_manifest(const uint8_t in[MARK_SIZE],
 Cmp5BackupReceipt out[CMP5_GROUP_MAX],uint32_t*count,
 Cmp5GroupReceipt*r){
 uint8_t digest[32];
 hash(in,BODY_DIGEST_OFFSET,digest);
 if(memcmp(in,group_magic,8)||memcmp(digest,in+BODY_DIGEST_OFFSET,32)||
    in[12]||in[13]||in[14]||in[15])return 0;
 for(unsigned j=BODY_DIGEST_OFFSET+32u;j<MARK_SIZE;j++)if(in[j])return 0;
 uint32_t n=g32(in+8);
 if(n<2||n>CMP5_GROUP_MAX)return 0;
 for(unsigned j=24u+n*40u;j<BODY_DIGEST_OFFSET;j++)if(in[j])return 0;
 uint64_t total=0;
 for(unsigned i=0;i<n;i++){
  out[i].bytes=g64(in+24u+i*40u);
  memcpy(out[i].sha256,in+24u+i*40u+8u,32);
  out[i].published=1;
  if(!out[i].bytes||out[i].bytes>MAX_TOTAL-total)return 0;
  total+=out[i].bytes;
 }
 if(total!=g64(in+16))return 0;
 *count=n;
 if(r){memset(r,0,sizeof(*r));r->count=n;r->bytes=total;
  memcpy(r->manifest_sha256,digest,32);}
 return 1;
}
static int read_manifest(int dir,const char*id,int restored,
 uint8_t contents[MARK_SIZE],Cmp5BackupReceipt receipts[CMP5_GROUP_MAX],
 uint32_t*count,Cmp5GroupReceipt*result){
 char name[48];marker_name(name,id,restored);
 int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 if(fd<0)return 0;
 struct stat before,after,named;
 const int valid=fstat(fd,&before)==0&&S_ISREG(before.st_mode)&&
  before.st_nlink==1&&before.st_size==(off_t)MARK_SIZE&&
  read_exact(fd,contents,MARK_SIZE)&&fstat(fd,&after)==0&&
  fstatat(dir,name,&named,AT_SYMLINK_NOFOLLOW)==0&&
  same_file(&before,&after)&&same_file(&before,&named)&&
  named.st_nlink==1&&
  parse_manifest(contents,receipts,count,result);
 close(fd);return valid;
}
static int publish_manifest(int dir,const char*id,int restored,
 const uint8_t manifest[MARK_SIZE],Cmp5CancelCheck cancel,void*ctx){
 char name[48];marker_name(name,id,restored);
 char temp[64];(void)snprintf(temp,sizeof(temp),".%s.part",name);
 int fd=openat(dir,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
 if(fd<0)return 0;
 int ok=write_exact(fd,manifest,MARK_SIZE)&&fsync(fd)==0;
 /* Keep the writer fd until AFTER publication. A different private file
  * can replace .part while fsync runs; linkat(temp,name) would otherwise
  * publish that unrelated inode. Do not delete any name on failure. */
 struct stat written={0},staged={0};
 if(ok)ok=fstat(fd,&written)==0&&S_ISREG(written.st_mode)&&
   written.st_nlink==1&&written.st_size==(off_t)MARK_SIZE&&
   fstatat(dir,temp,&staged,AT_SYMLINK_NOFOLLOW)==0&&
   same_file(&written,&staged);
 if(cancelled(cancel,ctx)||!ok){close(fd);return 0;}
 if(linkat(dir,temp,dir,name,0)!=0){close(fd);return 0;}
 /* A replacement during publication must never receive the legitimate
  * COMMIT receipt or be unlinked as if it were our own staging file. */
 struct stat published={0},current={0},current_stage={0};
 ok=fstat(fd,&current)==0&&current.st_nlink==2&&
   fstatat(dir,name,&published,AT_SYMLINK_NOFOLLOW)==0&&
   fstatat(dir,temp,&current_stage,AT_SYMLINK_NOFOLLOW)==0&&
   same_file(&current,&published)&&same_file(&current,&current_stage);
 if(!ok){close(fd);return 0;}
 /* This last pathname unlink cannot be conditional on inode identity
  * in portable POSIX. The private 0700 workspace and exclusive flock
  * serialize cooperating writers; an uncooperative same-user attacker
  * can still race this final syscall. */
 ok=unlinkat(dir,temp,0)==0;
 close(fd);
 return ok&&fsync(dir)==0;
}
static int raw_restored(int dir,const char*name,uint64_t size,
 const uint8_t expected[32],Cmp5CancelCheck cancel,void*ctx){
 int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 if(fd<0)return 0;
 struct stat before,after,named;
 int ok=fstat(fd,&before)==0&&S_ISREG(before.st_mode)&&
   before.st_nlink==1&&before.st_size>0&&(uint64_t)before.st_size==size;
 Cmp5Sha256 h;cmp5_sha256_init(&h);
 uint8_t block[BLOCK];
 for(uint64_t done=0;ok&&done<size;){
  if(cancelled(cancel,ctx)){ok=0;break;}
  size_t n=(size_t)(size-done>sizeof(block)?sizeof(block):size-done);
  ok=read_exact(fd,block,n);
  if(ok)cmp5_sha256_update(&h,block,n);
  done+=n;
 }
 uint8_t digest[32];if(ok)cmp5_sha256_final(&h,digest);
 if(ok)ok=memcmp(digest,expected,32)==0&&fstat(fd,&after)==0&&
  fstatat(dir,name,&named,AT_SYMLINK_NOFOLLOW)==0&&
  same_file(&before,&after)&&same_file(&before,&named)&&
  named.st_nlink==1&&!cancelled(cancel,ctx);
 close(fd);return ok;
}
/* All direct receipts must reject incomplete or ambiguous namespaces. */
static int verification_namespace_clean(int dir,const char*id,
                                        int restored,uint32_t count);
int cmp5_verify_group(const char*workspace,const char*id,
 Cmp5GroupReceipt*r,Cmp5CancelCheck cancel,void*ctx){
 if(r)memset(r,0,sizeof(*r));
 if(!valid_id(id)||cancelled(cancel,ctx))return 0;
 int dir=safe_dir(workspace);
 if(dir<0)return 0;
 struct stat dir_before={0};
 if(fstat(dir,&dir_before)!=0){close(dir);return 0;}
 uint8_t marker[MARK_SIZE];Cmp5BackupReceipt part[CMP5_GROUP_MAX];
 Cmp5GroupReceipt found;uint32_t count=0;
 int ok=read_manifest(dir,id,0,marker,part,&count,&found);
 for(unsigned i=0;ok&&i<count;i++){
  if(cancelled(cancel,ctx)){ok=0;break;}
  char name[48];member_name(name,id,i);
  Cmp5BackupReceipt actual;
  ok=cmp5_verify_backup(workspace,name,&actual,cancel,ctx)&&
   actual.bytes==part[i].bytes&&
   memcmp(actual.sha256,part[i].sha256,32)==0;
 }
 /* The marker may be moved after we parsed it but while member capsules
  * were hashed. Only publish VERIFIED when the COMMIT still exists under
  * the anchored directory and contains the same manifest. */
 if(ok){
  uint8_t final_marker[MARK_SIZE];
  Cmp5BackupReceipt final_part[CMP5_GROUP_MAX];
  uint32_t final_count=0;
  ok=read_manifest(dir,id,0,final_marker,final_part,&final_count,NULL)&&
   final_count==count&&memcmp(marker,final_marker,MARK_SIZE)==0&&
   verification_namespace_clean(dir,id,0,count)&&
   !cancelled(cancel,ctx)&&
   workspace_unchanged(dir,workspace,&dir_before);
 }
 close(dir);
 if(ok&&r)*r=found;
 return ok;
}
/* Require an empty transaction namespace BEFORE the first capsule is
 * copied. A stale part for member 3 must not allow us to publish members
 * 1 and 2 before discovering the conflict. This is a private, read-only
 * preflight: it does not delete, repair or rename residual files.
 * Changes by another process after the scan are still handled by O_EXCL
 * and fail-closed recovery. The caller holds a directory-wide,
 * nonblocking exclusive lock through COMMIT and final verification. */
static int empty_group_namespace(int dir,const char*id){
 const size_t len=strlen(id);
 /* dup(dir) shares a directory stream offset with the caller. An
  * earlier scan may have left that offset at EOF and allowed unknown
  * capsules or leftover .part entries to go unnoticed. Open a separate
  * descriptor for this SAME anchored directory, with its own cursor. */
 int duplicate=openat(dir,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(duplicate<0)return 0;
 DIR*scan=fdopendir(duplicate);
 if(!scan){close(duplicate);return 0;}
 int ok=1;unsigned visited=0;
 for(;;){
  errno=0;
  struct dirent*entry=readdir(scan);
  if(!entry){if(errno!=0)ok=0;break;}
  if(++visited>4096u){ok=0;break;}
  const char*name=entry->d_name;
  const int published=strncmp(name,id,len)==0&&
    name[len]=='_'&&name[len+1]=='_';
  const int staged=name[0]=='.'&&
    strncmp(name+1,id,len)==0&&
    name[len+1]=='_'&&name[len+2]=='_';
  if(published||staged){ok=0;break;}
 }
 if(closedir(scan)!=0)ok=0;
 return ok;
}

/* A caller can omit a visible JPG/JXR/DAT variant while still passing
 * structural validation. Require the exact descriptor-anchored companion
 * inventory, not merely a plausible subset, before publishing a transaction.
 * This is only visible filesystem coverage; PS5 gallery indexes remain
 * unverified and no deletion is authorized by it. */
static int matches_visible_companions(const char*root,
 const Cmp5MediaCandidate*parts,uint32_t count,
 Cmp5CancelCheck cancel,void*ctx){
 if(!root||!cmp5_validate_capture_group(parts,count)||
    cancelled(cancel,ctx))return 0;
 const Cmp5MediaCandidate*primary=NULL;
 for(uint32_t i=0;i<count;i++){
  /* The external thumbnail is never the selected primary capture. */
  if(parts[i].media_type==1u||parts[i].media_type==2u){
   primary=&parts[i];break;
  }
 }
 if(!primary)return 0;
 Cmp5MediaCandidate visible[CMP5_GROUP_MAX]={{0}};
 uint32_t observed=0;
 int ok=cmp5_plan_capture_group(root,primary,visible,&observed,cancel,ctx)&&
   observed==count;
 unsigned matched=0u;
 for(uint32_t i=0;ok&&i<count;i++){
  unsigned found=0u;
  for(uint32_t j=0;j<count;j++){
   const Cmp5MediaCandidate*a=&parts[i],*b=&visible[j];
   if(strcmp(a->preview_path,b->preview_path)==0&&
      a->media_type==b->media_type&&cmp5_snapshot_same(a,b)){
    if(matched&(1u<<j)){ok=0;break;}
    matched|=1u<<j;found=1u;break;
   }
  }
  if(!found)ok=0;
 }
 memset(visible,0,sizeof(visible));
 return ok&&!cancelled(cancel,ctx);
}
/* Entire grouped transaction is serialized by one advisory, nonblocking
 * lock on the private workspace inode. Hold it through the COMMIT verify.
 * The lock covers ALL group IDs here, not only a single requested ID.
 * External programs ignoring flock are still untrusted writers. */
int cmp5_backup_group(const char*root,const Cmp5MediaCandidate*parts,
 uint32_t count,const char*workspace,const char*id,
 Cmp5GroupReceipt*r,Cmp5CancelCheck cancel,void*ctx){
 if(r)memset(r,0,sizeof(*r));
 if(!r||!valid_id(id)||!matches_visible_companions(root,parts,count,
      cancel,ctx))return 0;
 int dir=safe_dir(workspace);
 if(dir<0)return 0;
 if(flock(dir,LOCK_EX|LOCK_NB)!=0){close(dir);return 0;}
 int ok=0;
 char commit_name[48];marker_name(commit_name,id,0);
 struct stat st;
 int clear=empty_group_namespace(dir,id)&&
   fstatat(dir,commit_name,&st,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT;
 uint64_t total=0u;
 for(unsigned i=0;clear&&i<count;i++){
  if(parts[i].bytes>MAX_TOTAL-total)clear=0;
  else total+=parts[i].bytes;
 }
 if(clear)clear=cmp5_workspace_has_capacity(dir,total,count+1u);
 if(!clear)goto backup_done;
 Cmp5BackupReceipt pieces[CMP5_GROUP_MAX]={{0}};
 for(unsigned i=0;i<count;i++){
  if(cancelled(cancel,ctx))goto backup_done;
  char name[48];member_name(name,id,i);
  if(!cmp5_backup_media(root,&parts[i],workspace,name,&pieces[i],cancel,ctx))
   goto backup_done; /* No COMMIT for incomplete groups. */
 }
 for(unsigned i=0;i<count;i++){
  char name[48];member_name(name,id,i);
  Cmp5BackupReceipt check;
  if(!cmp5_verify_backup(workspace,name,&check,cancel,ctx)||
     check.bytes!=pieces[i].bytes||
     memcmp(check.sha256,pieces[i].sha256,32)!=0)goto backup_done;
 }
 if(!matches_visible_companions(root,parts,count,cancel,ctx))
  goto backup_done;
 uint8_t manifest[MARK_SIZE];manifest_header(manifest,pieces,count);
 ok=publish_manifest(dir,id,0,manifest,cancel,ctx);
 if(ok)ok=cmp5_verify_group(workspace,id,r,cancel,ctx);
backup_done:
 close(dir); /* releases flock after verification */
 return ok;
}
int cmp5_verify_restored_group(const char*workspace,const char*id,
 Cmp5GroupReceipt*r,Cmp5CancelCheck cancel,void*ctx){
 if(r)memset(r,0,sizeof(*r));
 if(!valid_id(id)||cancelled(cancel,ctx))return 0;
 int dir=safe_dir(workspace);if(dir<0)return 0;
 struct stat dir_before={0};
 if(fstat(dir,&dir_before)!=0){close(dir);return 0;}
 uint8_t marker[MARK_SIZE];Cmp5BackupReceipt parts[CMP5_GROUP_MAX];
 Cmp5GroupReceipt result;uint32_t count=0;
 int ok=read_manifest(dir,id,1,marker,parts,&count,&result);
 for(unsigned i=0;ok&&i<count;i++){
  char name[48];member_name(name,id,i);
  ok=raw_restored(dir,name,parts[i].bytes,parts[i].sha256,cancel,ctx);
 }
 /* Recheck the RESTORED marker once all members have been hashed. A marker
  * removed mid-verification invalidates an otherwise byte-identical set. */
 if(ok){
  uint8_t final_marker[MARK_SIZE];
  Cmp5BackupReceipt final_parts[CMP5_GROUP_MAX];
  uint32_t final_count=0;
  ok=read_manifest(dir,id,1,final_marker,final_parts,&final_count,NULL)&&
   final_count==count&&memcmp(marker,final_marker,MARK_SIZE)==0&&
   verification_namespace_clean(dir,id,1,count)&&
   !cancelled(cancel,ctx)&&
   workspace_unchanged(dir,workspace,&dir_before);
 }
 close(dir);if(ok&&r)*r=result;return ok;
}
/* Coordinate with grouped backups using a shared source lock and an
 * exclusive target lock. Both are nonblocking, so reversed concurrent
 * operations cannot deadlock. Never write inside source or PS5 roots. */
int cmp5_restore_group(const char*backup,const char*id,const char*target,
 Cmp5GroupReceipt*r,Cmp5CancelCheck cancel,void*ctx){
 if(r)memset(r,0,sizeof(*r));
 if(!r||!valid_id(id)||!backup||!target||
    strcmp(backup,target)==0||cancelled(cancel,ctx))return 0;
 int from=safe_dir(backup);
 if(from<0)return 0;
 int dest=safe_dir(target);
 if(dest<0){close(from);return 0;}
 struct stat src_dir={0},dst_dir={0};
 int ok=fstat(from,&src_dir)==0&&fstat(dest,&dst_dir)==0&&
   !(src_dir.st_dev==dst_dir.st_dev&&src_dir.st_ino==dst_dir.st_ino)&&
   flock(from,LOCK_SH|LOCK_NB)==0&&flock(dest,LOCK_EX|LOCK_NB)==0;
 if(!ok)goto restore_done;
 Cmp5RecoveryReport source_state={0};
 ok=cmp5_inspect_group_recovery(backup,id,&source_state)&&
   source_state.state==CMP5_RECOVERY_VERIFIED&&!cancelled(cancel,ctx);
 if(!ok)goto restore_done;
 uint8_t manifest[MARK_SIZE];
 Cmp5BackupReceipt pieces[CMP5_GROUP_MAX];
 uint32_t count=0;
 ok=read_manifest(from,id,0,manifest,pieces,&count,NULL);
 if(!ok)goto restore_done;
 if(!empty_group_namespace(dest,id)){ok=0;goto restore_done;}
 char commit[48];marker_name(commit,id,1);
 struct stat st={0};
 ok=fstatat(dest,commit,&st,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT;
 for(unsigned i=0;ok&&i<count;i++){
  char name[48];member_name(name,id,i);
  ok=fstatat(dest,name,&st,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT;
 }
 if(ok){
  uint64_t total=0u;
  for(unsigned i=0;i<count;i++){
   if(pieces[i].bytes>MAX_TOTAL-total){ok=0;break;}
   total+=pieces[i].bytes;
  }
  if(ok)ok=cmp5_workspace_has_capacity(dest,total,count+1u);
 }
 if(!ok)goto restore_done;
 for(unsigned i=0;i<count;i++){
  if(cancelled(cancel,ctx)){ok=0;goto restore_done;}
  char name[48];member_name(name,id,i);
  Cmp5BackupReceipt receipt;
  if(!cmp5_restore_backup(backup,name,target,name,&receipt,cancel,ctx)||
     receipt.bytes!=pieces[i].bytes||
     memcmp(receipt.sha256,pieces[i].sha256,32)!=0){
   ok=0;goto restore_done;
  }
 }
 for(unsigned i=0;ok&&i<count;i++){
  char name[48];member_name(name,id,i);
  ok=raw_restored(dest,name,pieces[i].bytes,pieces[i].sha256,cancel,ctx);
 }
 /* Re-audit source while locked, before the final target RESTORED marker. */
 if(ok){
  Cmp5RecoveryReport stable={0};
  uint8_t current_manifest[MARK_SIZE];
  Cmp5BackupReceipt current_pieces[CMP5_GROUP_MAX];
  uint32_t current_count=0;
  ok=cmp5_inspect_group_recovery(backup,id,&stable)&&
     stable.state==CMP5_RECOVERY_VERIFIED&&
     read_manifest(from,id,0,current_manifest,current_pieces,
                   &current_count,NULL)&&
     current_count==count&&
     memcmp(current_manifest,manifest,MARK_SIZE)==0&&
     !cancelled(cancel,ctx);
 }
 if(ok)ok=publish_manifest(dest,id,1,manifest,cancel,ctx);
 if(ok)ok=cmp5_verify_restored_group(target,id,r,cancel,ctx);
restore_done:
 close(dest);
 close(from);
 return ok;
}

/* Recovery audits inspect only a private, pre-existing 0700 workspace.
 * No PS5 gallery file is opened for write or modified by these routines. */
static int recovery_entry(int dir,const char*name,int*present,int*unsafe){
 struct stat st;*present=0;
 if(fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW)!=0)
  return errno==ENOENT;
 *present=1;
 if(!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<0||
    (uint64_t)st.st_size>MAX_TOTAL+64u)*unsafe=1;
 return 1;
}
/* Reject unknown files within a group namespace, e.g. ALBUM__5,
 * .ALBUM__5.part, a foreign commit marker or an undeclared sidecar.
 * Checking only the four expected names would silently ignore them. */
static int expected_recovery_name(const char*name,const char*id,int restored){
 char candidate[48],stage[64];
 for(unsigned i=0;i<CMP5_GROUP_MAX+1u;i++){
  if(i==CMP5_GROUP_MAX)marker_name(candidate,id,restored);
  else member_name(candidate,id,i);
  if(strcmp(name,candidate)==0)return 1;
  (void)snprintf(stage,sizeof(stage),".%s.part",candidate);
  if(strcmp(name,stage)==0)return 1;
 }
 return 0;
}
static int inspect_namespace(int dir,const char*id,int restored,
 uint32_t*unexpected){
 *unexpected=0;
 const size_t length=strlen(id);
 /* dup(dir) shares a directory stream offset with the caller. An
  * earlier scan may have left that offset at EOF and allowed unknown
  * capsules or leftover .part entries to go unnoticed. Open a separate
  * descriptor for this SAME anchored directory, with its own cursor. */
 int duplicate=openat(dir,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(duplicate<0)return 0;
 DIR*scan=fdopendir(duplicate);
 if(!scan){close(duplicate);return 0;}
 int ok=1;
 unsigned visited=0;
 for(;;){
  errno=0;
  struct dirent*entry=readdir(scan);
  if(!entry){if(errno!=0)ok=0;break;}
  if(++visited>4096u){ok=0;break;}
  const char*name=entry->d_name;
  const int ordinary=strncmp(name,id,length)==0&&
    name[length]=='_'&&name[length+1]=='_';
  const int staged=name[0]=='.'&&
    strncmp(name+1,id,length)==0&&
    name[length+1]=='_'&&name[length+2]=='_';
  if((ordinary||staged)&&!expected_recovery_name(name,id,restored)){
   if(*unexpected==UINT32_MAX){ok=0;break;}
   (*unexpected)++;
  }
 }
 if(closedir(scan)!=0)ok=0;
 return ok;
}
/* SHA equality cannot turn an interrupted or surplus-containing
 * group into a verified backup. All stage names and members above the
 * manifest count must be ABSENT, including symlinks. */
static int verification_namespace_clean(int dir,const char*id,
                                        int restored,uint32_t count){
 if(count>CMP5_GROUP_MAX)return 0;
 uint32_t unexpected=0;
 if(!inspect_namespace(dir,id,restored,&unexpected)||unexpected)return 0;
 for(unsigned i=0;i<=CMP5_GROUP_MAX;i++){
  char name[48],stage[64];
  if(i==CMP5_GROUP_MAX)marker_name(name,id,restored);
  else member_name(name,id,i);
  (void)snprintf(stage,sizeof(stage),".%s.part",name);
  struct stat st;
  errno=0;
  if(fstatat(dir,stage,&st,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT)
   return 0;
  if(i>=count&&i<CMP5_GROUP_MAX){
   errno=0;
   if(fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT)
    return 0;
  }
 }
 return 1;
}
static int inspect_recovery(const char*workspace,const char*id,int restored,
 Cmp5RecoveryReport*report){
 if(!report)return 0;
 memset(report,0,sizeof(*report));
 if(!valid_id(id))return 0;
 int dir=safe_dir(workspace);
 if(dir<0)return 0;
 struct stat dir_before={0};
 if(fstat(dir,&dir_before)!=0){close(dir);return 0;}
 Cmp5RecoveryReport result={0};
 int unsafe=0,present=0,ok=1;
 char name[48],stage[64];
 marker_name(name,id,restored);
 ok=recovery_entry(dir,name,&present,&unsafe);
 result.marker_present=present;
 if(ok){
  (void)snprintf(stage,sizeof(stage),".%s.part",name);
  ok=recovery_entry(dir,stage,&present,&unsafe);
  result.staged_parts+=(uint32_t)present;
 }
 for(unsigned i=0;ok&&i<CMP5_GROUP_MAX;i++){
  member_name(name,id,i);
  ok=recovery_entry(dir,name,&present,&unsafe);
  result.published_parts+=(uint32_t)present;
  if(!ok)break;
  (void)snprintf(stage,sizeof(stage),".%s.part",name);
  ok=recovery_entry(dir,stage,&present,&unsafe);
  result.staged_parts+=(uint32_t)present;
 }
 if(ok)ok=inspect_namespace(dir,id,restored,&result.unexpected_entries);
 if(!ok){close(dir);return 0;}
 if(unsafe||result.unexpected_entries)result.state=CMP5_RECOVERY_INVALID;
 else if(result.marker_present){
  Cmp5GroupReceipt verified={0};
  /* The commit marker and EVERY member must pass digest validation.
   * Staged and surplus files invalidate even an otherwise valid receipt. */
  const int complete=restored?
    cmp5_verify_restored_group(workspace,id,&verified,NULL,NULL):
    cmp5_verify_group(workspace,id,&verified,NULL,NULL);
  result.state=complete&&result.staged_parts==0u&&
    result.published_parts==verified.count?
     CMP5_RECOVERY_VERIFIED:CMP5_RECOVERY_INVALID;
 }else if(result.published_parts||result.staged_parts)
  result.state=CMP5_RECOVERY_INCOMPLETE;
 else result.state=CMP5_RECOVERY_EMPTY;
 /* Even EMPTY is an identity-sensitive claim: an attacker may replace the
  * directory while readdir consumes the original anchored descriptor.
  * Do not report EMPTY/VERIFIED for a different workspace at the path. */
 const int stable=workspace_unchanged(dir,workspace,&dir_before);
 close(dir);
 if(!stable)return 0;
 *report=result;
 return 1;
}
int cmp5_inspect_group_recovery(const char*workspace,const char*id,
 Cmp5RecoveryReport*report){
 return inspect_recovery(workspace,id,0,report);
}
int cmp5_inspect_restored_group_recovery(const char*workspace,const char*id,
 Cmp5RecoveryReport*report){
 return inspect_recovery(workspace,id,1,report);
}

/* Correlate two independently valid private workspaces. No PS5 gallery
 * access, writing, repair, or file cleanup. IDs can differ because the
 * receipt commits to ordered file sizes and SHA-256 digests, not names. */
int cmp5_inspect_backup_restore_pair(const char*backup,const char*backup_id,
 const char*restore,const char*restore_id,Cmp5RecoveryPairReport*report){
 if(!report)return 0;
 memset(report,0,sizeof(*report));
 if(!backup||!restore||!valid_id(backup_id)||!valid_id(restore_id))return 0;
 int from=safe_dir(backup);
 if(from<0)return 0;
 int to=safe_dir(restore);
 if(to<0){close(from);return 0;}
 struct stat from_before,to_before,from_after,to_after;
 int ok=fstat(from,&from_before)==0&&fstat(to,&to_before)==0&&
   !(from_before.st_dev==to_before.st_dev&&
     from_before.st_ino==to_before.st_ino);
 if(!ok){close(to);close(from);return 0;}
 Cmp5RecoveryPairReport found={0};
 ok=cmp5_inspect_group_recovery(backup,backup_id,&found.backup)&&
   cmp5_inspect_restored_group_recovery(restore,restore_id,&found.restored);
 if(ok){
  if(found.backup.state==CMP5_RECOVERY_INVALID||
     found.restored.state==CMP5_RECOVERY_INVALID){
   found.state=CMP5_PAIR_INVALID;
  }else if(found.backup.state!=CMP5_RECOVERY_VERIFIED||
           found.restored.state!=CMP5_RECOVERY_VERIFIED){
   found.state=CMP5_PAIR_NOT_READY;
  }else{
   Cmp5GroupReceipt original={0},recovered={0};
   if(cmp5_verify_group(backup,backup_id,&original,NULL,NULL)&&
      cmp5_verify_restored_group(restore,restore_id,&recovered,NULL,NULL)){
    const int matches=original.count==recovered.count&&
      original.bytes==recovered.bytes&&
      memcmp(original.manifest_sha256,recovered.manifest_sha256,32)==0;
    found.state=matches?CMP5_PAIR_MATCH:CMP5_PAIR_MISMATCH;
   }else found.state=CMP5_PAIR_UNVERIFIABLE;
   /* A directory can mutate while we hash large capsules. Recheck both
    * namespaces before claiming an end-to-end match. */
   if(found.state==CMP5_PAIR_MATCH){
    Cmp5RecoveryReport backup_after={0},restore_after={0};
    if(!cmp5_inspect_group_recovery(backup,backup_id,&backup_after)||
       !cmp5_inspect_restored_group_recovery(restore,restore_id,&restore_after)||
       backup_after.state!=CMP5_RECOVERY_VERIFIED||
       restore_after.state!=CMP5_RECOVERY_VERIFIED)
     found.state=CMP5_PAIR_UNVERIFIABLE;
   }
  }
 }
 /* Both path identities and directory contents must remain stable between
  * the initial check and publication. A rename/alias cannot validate a
  * different workspace than the one initially opened. */
 int check_from=safe_dir(backup),check_to=safe_dir(restore);
 struct stat current_from,current_to;
 if(check_from<0||check_to<0)ok=0;
 else if(fstat(check_from,&current_from)!=0||
         fstat(check_to,&current_to)!=0||
         !same_file(&current_from,&from_before)||
         !same_file(&current_to,&to_before))ok=0;
 if(check_from>=0)close(check_from);
 if(check_to>=0)close(check_to);
 if(fstat(from,&from_after)!=0||fstat(to,&to_after)!=0||
    !same_file(&from_before,&from_after)||
    !same_file(&to_before,&to_after))ok=0;
 close(to);close(from);
 if(!ok)return 0;
 *report=found;
 return 1;
}
