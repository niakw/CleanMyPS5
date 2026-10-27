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
/* Private-only mapping of original Gallery locators AND snapshot identities
 * to ordered capsules. v2: path[256], bytes[8], type[4],
 * dev[8], inode[8], ctime[8], mtime[8], mtime_ns[4], ctime_ns[4].
 * Native and host read-only mapping; no Gallery write paths. */
#define ORIGIN_RECORD_SIZE 308u
#define ORIGIN_HEADER_SIZE 44u
#define ORIGIN_DIGEST_OFFSET (ORIGIN_HEADER_SIZE+CMP5_GROUP_MAX*ORIGIN_RECORD_SIZE)
#define ORIGIN_SIZE (ORIGIN_DIGEST_OFFSET+32u)
#define MAX_TOTAL (512ULL*1024ULL*1024ULL)
#define BLOCK 65536u
static const uint8_t group_magic[8]={'C','M','P','5','G','R','P','2'};
static const uint8_t origin_magic[8]={'C','M','P','5','M','A','P','2'};
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
 /* Revalidation must preserve ownership as well as permissions and inode.
  * The initial geteuid() check alone does not attest ownership after a
  * potentially long manifest, origin-map or restored-file read. */
 return nsec&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
  a->st_uid==b->st_uid&&
  a->st_size==b->st_size&&a->st_mtime==b->st_mtime&&
  a->st_ctime==b->st_ctime&&a->st_mode==b->st_mode&&
  a->st_nlink==b->st_nlink;
}
/* A descriptor survives rename/unlink of its pathname. Do not report a
 * verified group for bytes in an orphaned directory when the caller's
 * selected private workspace now names another one (or no directory). */
/* A restore can change directory size/mtime. Keep the opened private
 * inode pinned while requiring the original PATH to name it, with
 * unchanged owner, access mode and link count. Never follow symlinks. */
static int mutable_workspace_current(int fd,const char*path,
                                     const struct stat*initial){
 struct stat held={0},named={0};
 if(fd<0||!path||!initial||fstat(fd,&held)!=0||
    !S_ISDIR(held.st_mode)||
    held.st_dev!=initial->st_dev||held.st_ino!=initial->st_ino||
    held.st_uid!=initial->st_uid||held.st_mode!=initial->st_mode||
    held.st_nlink!=initial->st_nlink)return 0;
 int reopened=safe_dir(path);
 if(reopened<0)return 0;
 const int ok=fstat(reopened,&named)==0&&S_ISDIR(named.st_mode)&&
   named.st_dev==held.st_dev&&named.st_ino==held.st_ino&&
   named.st_uid==held.st_uid&&named.st_mode==held.st_mode&&
   named.st_nlink==held.st_nlink;
 close(reopened);
 return ok;
}
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
  before.st_uid==geteuid()&&(before.st_mode&0077)==0&&
  before.st_nlink==1&&before.st_size==(off_t)MARK_SIZE&&
  read_exact(fd,contents,MARK_SIZE)&&fstat(fd,&after)==0&&
  fstatat(dir,name,&named,AT_SYMLINK_NOFOLLOW)==0&&
  same_file(&before,&after)&&same_file(&before,&named)&&
  named.st_nlink==1&&
  parse_manifest(contents,receipts,count,result);
 close(fd);return valid;
}
/* An origin map is necessary for any later, explicit repair plan:
 * digest-only capsules do not say which original Gallery file each
 * restored member belonged to. The sensitive locators remain in a private
 * 0700 workspace and are NEVER inserted into native UI or public receipts.
 * It binds to the SHA-256 digest of the committed ordered group manifest,
 * not merely its group ID or a filename. */
static int parse_origin_map(const uint8_t raw[ORIGIN_SIZE],
 const uint8_t manifest[MARK_SIZE],const Cmp5BackupReceipt *pieces,
 uint32_t count,Cmp5OriginMap*out){
 uint8_t calculated[32];
 hash(raw,ORIGIN_DIGEST_OFFSET,calculated);
 if(memcmp(raw,origin_magic,8)!=0||g32(raw+8)!=count||
    count<2u||count>CMP5_GROUP_MAX||
    memcmp(raw+12,manifest+BODY_DIGEST_OFFSET,32)!=0||
    memcmp(raw+ORIGIN_DIGEST_OFFSET,calculated,32)!=0)return 0;
 Cmp5OriginMap proposed={0};
 Cmp5MediaCandidate snapshot[CMP5_GROUP_MAX]={{0}};
 proposed.count=count;
 for(unsigned i=0;i<CMP5_GROUP_MAX;i++){
  const uint8_t*record=raw+ORIGIN_HEADER_SIZE+i*ORIGIN_RECORD_SIZE;
  if(i>=count){
   for(unsigned j=0;j<ORIGIN_RECORD_SIZE;j++)if(record[j])return 0;
   continue;
  }
  const uint8_t*end=memchr(record,0,256u);
  if(!end||end==record)return 0;
  for(const uint8_t*cursor=end+1;cursor<record+256u;cursor++)
   if(*cursor)return 0;
  const uint64_t bytes=g64(record+256u);
  const uint32_t media_type=g32(record+264u);
  const uint64_t device=g64(record+268u),inode=g64(record+276u);
  const uint64_t ctime=g64(record+284u),mtime=g64(record+292u);
  const uint32_t mtime_ns=g32(record+300u),ctime_ns=g32(record+304u);
  if(bytes!=pieces[i].bytes||!bytes||!device||!inode||
     !ctime||!mtime||ctime>INT64_MAX||mtime>INT64_MAX||
     mtime_ns>=1000000000u||ctime_ns>=1000000000u)return 0;
  memcpy(proposed.entries[i].path,record,256u);
  proposed.entries[i].bytes=bytes;
  proposed.entries[i].media_type=media_type;
  proposed.entries[i].snapshot_device=device;
  proposed.entries[i].snapshot_inode=inode;
  proposed.entries[i].snapshot_ctime=(int64_t)ctime;
  proposed.entries[i].modified_at=(int64_t)mtime;
  proposed.entries[i].snapshot_mtime_nsec=mtime_ns;
  proposed.entries[i].snapshot_ctime_nsec=ctime_ns;
  memcpy(snapshot[i].preview_path,record,256u);
  snapshot[i].bytes=bytes;
  snapshot[i].media_type=media_type;
  snapshot[i].snapshot_device=device;
  snapshot[i].snapshot_inode=inode;
  snapshot[i].snapshot_ctime=(int64_t)ctime;
  snapshot[i].modified_at=(int64_t)mtime;
  snapshot[i].snapshot_mtime_nsec=mtime_ns;
  snapshot[i].snapshot_ctime_nsec=ctime_ns;
 }
 if(!cmp5_validate_capture_group(snapshot,count))return 0;
 if(out)*out=proposed;
 return 1;
}
static int read_origin_map(int dir,const char*id,
 const uint8_t manifest[MARK_SIZE],const Cmp5BackupReceipt*pieces,
 uint32_t count,Cmp5OriginMap*out){
 char name[48];origin_name(name,id);
 int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 if(fd<0)return 0;
 struct stat before={0},after={0},named={0};
 uint8_t raw[ORIGIN_SIZE];
 Cmp5OriginMap result={0};
 const int ok=fstat(fd,&before)==0&&S_ISREG(before.st_mode)&&
  before.st_uid==geteuid()&&(before.st_mode&0077)==0&&
  before.st_nlink==1&&before.st_size==(off_t)ORIGIN_SIZE&&
  read_exact(fd,raw,ORIGIN_SIZE)&&
  fstat(fd,&after)==0&&
  fstatat(dir,name,&named,AT_SYMLINK_NOFOLLOW)==0&&
  same_file(&before,&after)&&same_file(&before,&named)&&
  parse_origin_map(raw,manifest,pieces,count,&result);
 close(fd);
 if(ok&&out)*out=result;
 return ok;
}
/* Reject unsupported origin metadata BEFORE the first capsule copy,
 * rather than leaving a 512-MiB partial backup that cannot be committed. */
static int origin_snapshots_persistable(const Cmp5MediaCandidate*parts,
 uint32_t count){
 if(!parts||count<2u||count>CMP5_GROUP_MAX)return 0;
 for(uint32_t i=0;i<count;i++){
  if(!parts[i].snapshot_device||!parts[i].snapshot_inode||
     parts[i].snapshot_ctime<=0||parts[i].modified_at<=0||
     parts[i].snapshot_mtime_nsec>=1000000000u||
     parts[i].snapshot_ctime_nsec>=1000000000u)return 0;
 }
 return 1;
}
static int publish_origin_map(int dir,const char*id,
 const Cmp5MediaCandidate*parts,uint32_t count,
 const uint8_t manifest[MARK_SIZE],Cmp5CancelCheck cancel,void*ctx){
 if(!parts||count<2u||count>CMP5_GROUP_MAX||
    !cmp5_validate_capture_group(parts,count))return 0;
 uint8_t raw[ORIGIN_SIZE]={0};
 memcpy(raw,origin_magic,8);
 u32(raw+8,count);
 memcpy(raw+12,manifest+BODY_DIGEST_OFFSET,32u);
 for(unsigned i=0;i<count;i++){
  const char*path=parts[i].preview_path;
  const size_t length=strnlen(path,sizeof(parts[i].preview_path));
  if(!length||length>=sizeof(parts[i].preview_path))return 0;
  uint8_t*record=raw+ORIGIN_HEADER_SIZE+i*ORIGIN_RECORD_SIZE;
  if(!parts[i].snapshot_device||!parts[i].snapshot_inode||
     parts[i].snapshot_ctime<=0||parts[i].modified_at<=0||
     parts[i].snapshot_mtime_nsec>=1000000000u||
     parts[i].snapshot_ctime_nsec>=1000000000u)return 0;
  memcpy(record,path,length);
  u64(record+256u,parts[i].bytes);
  u32(record+264u,parts[i].media_type);
  u64(record+268u,parts[i].snapshot_device);
  u64(record+276u,parts[i].snapshot_inode);
  u64(record+284u,(uint64_t)parts[i].snapshot_ctime);
  u64(record+292u,(uint64_t)parts[i].modified_at);
  u32(record+300u,parts[i].snapshot_mtime_nsec);
  u32(record+304u,parts[i].snapshot_ctime_nsec);
 }
 hash(raw,ORIGIN_DIGEST_OFFSET,raw+ORIGIN_DIGEST_OFFSET);
 char name[48];origin_name(name,id);
 char temp[64];(void)snprintf(temp,sizeof(temp),".%s.part",name);
 int fd=openat(dir,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
 if(fd<0)return 0;
 int ok=write_exact(fd,raw,ORIGIN_SIZE)&&fsync(fd)==0;
 struct stat written={0},staged={0};
 if(ok)ok=fstat(fd,&written)==0&&S_ISREG(written.st_mode)&&
  written.st_uid==geteuid()&&(written.st_mode&0077)==0&&
  written.st_nlink==1&&written.st_size==(off_t)ORIGIN_SIZE&&
  fstatat(dir,temp,&staged,AT_SYMLINK_NOFOLLOW)==0&&
  same_file(&written,&staged);
 if(!ok||cancelled(cancel,ctx)){close(fd);return 0;}
 if(linkat(dir,temp,dir,name,0)!=0){close(fd);return 0;}
 struct stat published={0},current={0},stage_current={0};
 ok=fstat(fd,&current)==0&&current.st_nlink==2&&
  fstatat(dir,name,&published,AT_SYMLINK_NOFOLLOW)==0&&
  fstatat(dir,temp,&stage_current,AT_SYMLINK_NOFOLLOW)==0&&
  same_file(&current,&published)&&same_file(&current,&stage_current);
 if(ok)ok=unlinkat(dir,temp,0)==0;
 close(fd);
 return ok&&fsync(dir)==0;
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
/* Native group RESTORED writers are compiled only for a fixed-path,
 * private-only rehearsal; generic Gallery restoration remains excluded. */
#if !defined(CMP5_NATIVE_GROUP_BACKUP_ONLY) || defined(CMP5_NATIVE_GROUP_PRIVATE_REHEARSAL_ONLY)
static int raw_restored(int dir,const char*name,uint64_t size,
 const uint8_t expected[32],Cmp5CancelCheck cancel,void*ctx){
 int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 if(fd<0)return 0;
 struct stat before,after,named;
 int ok=fstat(fd,&before)==0&&S_ISREG(before.st_mode)&&
   before.st_uid==geteuid()&&(before.st_mode&0077)==0&&
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
#endif /* !CMP5_NATIVE_GROUP_BACKUP_ONLY */
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
   read_origin_map(dir,id,marker,part,count,NULL)&&
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
 if(!r||!valid_id(id)||!origin_snapshots_persistable(parts,count)||
    !matches_visible_companions(root,parts,count,cancel,ctx))return 0;
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
 /* Each backup writes count capsules, one private origin map, and COMMIT.
  * Do not under-budget the fourth member or misstate available space. */
 if(clear)clear=cmp5_workspace_has_capacity(dir,total,count+2u);
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
 /* Map first, then COMMIT: interrupted mappings alone never claim success.
  * Its sensitive original paths stay inside the private backup directory. */
 ok=publish_origin_map(dir,id,parts,count,manifest,cancel,ctx);
 if(ok)ok=publish_manifest(dir,id,0,manifest,cancel,ctx);
 if(ok)ok=cmp5_verify_group(workspace,id,r,cancel,ctx);
backup_done:
 close(dir); /* releases flock after verification */
 return ok;
}
/* Read-only post-restart recovery of the ordered source locators.
 * This read-only private-space audit is also used by the native title;
 * callers cannot use it to
 * write into a PS5 Gallery or alter Sony's separate media indexes. */
int cmp5_read_group_origin_map(const char*workspace,const char*id,
 Cmp5OriginMap*r,Cmp5CancelCheck cancel,void*ctx){
 if(r)memset(r,0,sizeof(*r));
 if(!r||!valid_id(id)||cancelled(cancel,ctx))return 0;
 Cmp5GroupReceipt verified={0};
 if(!cmp5_verify_group(workspace,id,&verified,cancel,ctx))return 0;
 int dir=safe_dir(workspace);
 if(dir<0)return 0;
 struct stat before={0};
 uint8_t marker[MARK_SIZE],final_marker[MARK_SIZE];
 Cmp5BackupReceipt receipts[CMP5_GROUP_MAX],final_parts[CMP5_GROUP_MAX];
 uint32_t count=0,final_count=0;
 Cmp5OriginMap restored={0};
 int ok=fstat(dir,&before)==0&&
  read_manifest(dir,id,0,marker,receipts,&count,NULL)&&
  count==verified.count&&
  read_origin_map(dir,id,marker,receipts,count,&restored)&&
  read_manifest(dir,id,0,final_marker,final_parts,&final_count,NULL)&&
  final_count==count&&memcmp(marker,final_marker,MARK_SIZE)==0&&
  verification_namespace_clean(dir,id,0,count)&&
  workspace_unchanged(dir,workspace,&before)&&
  !cancelled(cancel,ctx);
 close(dir);
 if(ok)*r=restored;
 return ok;
}
/* Re-open the entire parent path from the pinned test root. Holding
 * a directory descriptor protects it against symlinks, but that inode
 * survives a rename of the parent. MISSING is meaningful only if the
 * parent we probed still lives under the ORIGINAL complete path.
 * A valid existing file is subject to the same check before CHANGED or
 * UNCHANGED can be reported. No Gallery writes are made here. */
static int origin_parent_current(int root,const char*path,
                                 size_t prefix_len,int held_parent){
 if(root<0||held_parent<0||!path||path[0]!='/'||
    prefix_len>=256u)return 0;
 int current=openat(root,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(current<0)return 0;
 size_t start=1u;
 while(start<prefix_len){
  size_t end=start;
  while(end<prefix_len&&path[end]!='/')end++;
  const size_t len=end-start;
  if(len==0u||len>=256u||path[start]=='.'){
   close(current);return 0;
  }
  char part[256];
  memcpy(part,path+start,len);
  part[len]=0;
  int child=openat(current,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  close(current);
  if(child<0)return 0;
  current=child;
  start=end+1u;
 }
 struct stat held={0},named={0};
 const int valid=fstat(held_parent,&held)==0&&fstat(current,&named)==0&&
   S_ISDIR(held.st_mode)&&S_ISDIR(named.st_mode)&&
   held.st_dev==named.st_dev&&held.st_ino==named.st_ino&&
   held.st_uid==named.st_uid&&held.st_mode==named.st_mode;
 close(current);
 return valid;
}
/* Read-only source review based on *persisted* CMP5MAP2 metadata.
 * A file that now occupies an old pathname is not automatically the
 * original. This code never creates a source path or repairs Sony indexes.
 * Unknown indicates a failed/unsafe inspection, NOT permission to clean. */
static Cmp5OriginLiveState inspect_source_once(int root,
 const Cmp5OriginEntry*entry){
 if(root<0||!entry)return CMP5_ORIGIN_UNKNOWN;
 const size_t n=strnlen(entry->path,sizeof(entry->path));
 if(n<2u||n>=sizeof(entry->path)||entry->path[0]!='/')
  return CMP5_ORIGIN_UNKNOWN;
 char components[sizeof(entry->path)];
 memcpy(components,entry->path+1,n); /* includes terminating NUL */
 int current=openat(root,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(current<0)return CMP5_ORIGIN_UNKNOWN;
 char*save=NULL;
 Cmp5OriginLiveState state=CMP5_ORIGIN_UNKNOWN;
 for(char*part=strtok_r(components,"/",&save);part;){
  if(!*part||part[0]=='.'){close(current);return CMP5_ORIGIN_UNKNOWN;}
  char*next=strtok_r(NULL,"/",&save);
  const int fd=openat(current,part,O_RDONLY|O_NOFOLLOW|
                      (next?O_DIRECTORY:O_NONBLOCK));
  const int problem=errno;
  const size_t parent_prefix=(size_t)(part-components);
  if(fd<0){
   const int stable_missing=problem==ENOENT&&
     origin_parent_current(root,entry->path,parent_prefix,current);
   close(current);
   return stable_missing?CMP5_ORIGIN_MISSING:CMP5_ORIGIN_UNKNOWN;
  }
  if(!next){
   Cmp5MediaCandidate original={0};
   original.bytes=entry->bytes;
   original.media_type=entry->media_type;
   original.snapshot_device=entry->snapshot_device;
   original.snapshot_inode=entry->snapshot_inode;
   original.modified_at=entry->modified_at;
   original.snapshot_ctime=entry->snapshot_ctime;
   original.snapshot_mtime_nsec=entry->snapshot_mtime_nsec;
   original.snapshot_ctime_nsec=entry->snapshot_ctime_nsec;
   struct stat observed={0},repeat={0},named={0};
   /* Keep the parent descriptor open to check the final NAME. An open
    * descriptor outlives rename/unlink, so a pair of fstats cannot prove
    * that the saved Gallery path still points to the captured inode.
    * A path changed during the read is UNKNOWN, never restore evidence. */
   if(fstat(fd,&observed)==0&&fstat(fd,&repeat)==0&&
      fstatat(current,part,&named,AT_SYMLINK_NOFOLLOW)==0&&
      S_ISREG(named.st_mode)&&
      same_file(&observed,&repeat)&&same_file(&repeat,&named)&&
      origin_parent_current(root,entry->path,parent_prefix,current)){
    const int valid=cmp5_snapshot_matches(&original,&observed)&&
                    cmp5_snapshot_matches(&original,&repeat);
    state=valid?CMP5_ORIGIN_UNCHANGED:CMP5_ORIGIN_CHANGED;
   }
  }
  close(current);
  current=fd;
  part=next;
 }
 close(current);
 return state;
}
int cmp5_inspect_group_originals(const char*root,const char*workspace,
 const char*id,Cmp5OriginLiveState states[CMP5_GROUP_MAX],
 Cmp5CancelCheck cancel,void*ctx){
 if(states)memset(states,0,sizeof(*states)*CMP5_GROUP_MAX);
 if(!states||!root||!workspace||!valid_id(id)||cancelled(cancel,ctx))
  return 0;
 Cmp5OriginMap entries={0};
 if(!cmp5_read_group_origin_map(workspace,id,&entries,cancel,ctx))
  return 0;
 int dir=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(dir<0)return 0;
 struct stat root_before={0},root_after={0},root_visible={0};
 int ok=fstat(dir,&root_before)==0&&S_ISDIR(root_before.st_mode);
 Cmp5OriginLiveState inspected[CMP5_GROUP_MAX]={0};
 for(uint32_t i=0;ok&&i<entries.count;i++){
  if(cancelled(cancel,ctx)){ok=0;break;}
  const Cmp5OriginLiveState first=inspect_source_once(dir,&entries.entries[i]);
  const Cmp5OriginLiveState second=inspect_source_once(dir,&entries.entries[i]);
  inspected[i]=first==second?first:CMP5_ORIGIN_UNKNOWN;
 }
 int fresh=ok?open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW):-1;
 if(ok)ok=fresh>=0&&fstat(fresh,&root_visible)==0&&
   fstat(dir,&root_after)==0&&
   root_before.st_dev==root_after.st_dev&&
   root_before.st_ino==root_after.st_ino&&
   root_before.st_dev==root_visible.st_dev&&
   root_before.st_ino==root_visible.st_ino&&
   !cancelled(cancel,ctx);
 if(fresh>=0)close(fresh);
 close(dir);
 if(ok)memcpy(states,inspected,sizeof(inspected));
 return ok;
}
#if !defined(CMP5_NATIVE_GROUP_BACKUP_ONLY) || defined(CMP5_NATIVE_GROUP_PRIVATE_REHEARSAL_ONLY)
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
#ifdef CMP5_NATIVE_GROUP_PRIVATE_REHEARSAL_ONLY
 /* Native group reconstruction may write ONLY to the dedicated app-owned
  * private workspace. The group must be a locally minted opaque ID.
  * Generic host-only restorer remains inaccessible on the PS5 title. */
 if(strcmp(backup,"/download0/cmp5-private-backups")!=0||
    strcmp(target,"/download0/cmp5-private-rehearsals")!=0||
    strlen(id)!=20u||strncmp(id,"GRP_",4u)!=0)return 0;
 for(unsigned i=4u;i<20u;i++)
  if(!((id[i]>='0'&&id[i]<='9')||
       (id[i]>='A'&&id[i]<='F')))return 0;
#endif
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
 /* A locked descriptor survives renaming. Never copy into a newly
  * named, unlocked private directory created after our initial locks. */
 if(ok)ok=mutable_workspace_current(from,backup,&src_dir)&&
           mutable_workspace_current(dest,target,&dst_dir);
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
 if(!mutable_workspace_current(from,backup,&src_dir)||
    !mutable_workspace_current(dest,target,&dst_dir)){
  ok=0;goto restore_done;
 }
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
     mutable_workspace_current(from,backup,&src_dir)&&
     mutable_workspace_current(dest,target,&dst_dir)&&
     !cancelled(cancel,ctx);
 }
 if(ok)ok=publish_manifest(dest,id,1,manifest,cancel,ctx);
 if(ok)ok=mutable_workspace_current(from,backup,&src_dir)&&
           mutable_workspace_current(dest,target,&dst_dir);
 if(ok)ok=cmp5_verify_restored_group(target,id,r,cancel,ctx);
 if(ok&&!mutable_workspace_current(from,backup,&src_dir))ok=0;
 if(ok&&!mutable_workspace_current(dest,target,&dst_dir))ok=0;
 /* An already filled receipt cannot survive a late identity failure. */
 if(!ok&&r)memset(r,0,sizeof(*r));
restore_done:
 close(dest);
 close(from);
 return ok;
}

#endif /* !CMP5_NATIVE_GROUP_BACKUP_ONLY */
 /* Recovery audits inspect only a private, pre-existing 0700 workspace.
 * No PS5 gallery file is opened for write or modified by these routines. */
#if !defined(CMP5_NATIVE_GROUP_BACKUP_ONLY) || defined(CMP5_NATIVE_GROUP_PRIVATE_REHEARSAL_ONLY)
static int recovery_entry(int dir,const char*name,int*present,int*unsafe){
 struct stat st;*present=0;
 if(fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW)!=0)
  return errno==ENOENT;
 *present=1;
 if(!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||
    (st.st_mode&0077)!=0||st.st_nlink!=1||st.st_size<0||
    (uint64_t)st.st_size>MAX_TOTAL+64u)*unsafe=1;
 return 1;
}
#endif /* !CMP5_NATIVE_GROUP_BACKUP_ONLY */
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
 if(!restored){
  origin_name(candidate,id);
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
 if(!restored){
  /* A leftover private origin-map .part is an incomplete transaction. */
  char name[48],stage[64];struct stat st={0};
  origin_name(name,id);
  (void)snprintf(stage,sizeof(stage),".%s.part",name);
  errno=0;
  if(fstatat(dir,stage,&st,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT)
   return 0;
 }
 return 1;
}
#if !defined(CMP5_NATIVE_GROUP_BACKUP_ONLY) || defined(CMP5_NATIVE_GROUP_PRIVATE_REHEARSAL_ONLY)
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
 /* The origin map is published before COMMIT. A power loss between
  * those two publications leaves an INCOMPLETE private transaction,
  * not an empty namespace. No sensitive locator is returned in reports. */
 int origin_present=0,origin_stage_present=0;
 if(ok&&!restored){
  origin_name(name,id);
  ok=recovery_entry(dir,name,&origin_present,&unsafe);
  if(ok&&origin_present){
   struct stat origin={0};
   if(fstatat(dir,name,&origin,AT_SYMLINK_NOFOLLOW)!=0||
      !S_ISREG(origin.st_mode)||origin.st_nlink!=1||
      origin.st_uid!=geteuid()||(origin.st_mode&0077)!=0||
      origin.st_size!=(off_t)ORIGIN_SIZE)unsafe=1;
  }
  if(ok){
   (void)snprintf(stage,sizeof(stage),".%s.part",name);
   ok=recovery_entry(dir,stage,&origin_stage_present,&unsafe);
   result.staged_parts+=(uint32_t)origin_stage_present;
  }
  result.origin_map_present=origin_present;
  result.origin_map_staged=origin_stage_present;
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
 }else if(result.published_parts||result.staged_parts||origin_present)
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
#endif /* !CMP5_NATIVE_GROUP_BACKUP_ONLY */
