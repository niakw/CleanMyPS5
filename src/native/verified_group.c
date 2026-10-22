#define _DEFAULT_SOURCE
#include "verified_group.h"
#include "sha256.h"
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
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
#define MAX_TOTAL (512ULL*1024ULL*1024ULL)
#define BLOCK 65536u
static const uint8_t group_magic[8]={'C','M','P','5','G','R','P','2'};
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
 for(;id[i]&&i<31;i++){
  char c=id[i];
  if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return 0;
 }
 return i>0&&i<=31&&id[i]==0;
}
static void member_name(char out[48],const char*id,unsigned index){
 (void)snprintf(out,48,"%s__%u",id,index+1u);
}
static void marker_name(char out[48],const char*id,int restored){
 (void)snprintf(out,48,"%s__%s",id,restored?"RESTORED":"COMMIT");
}
static int safe_dir(const char*path){
 if(!path)return -1;
 int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return -1;
 struct stat st;
 if(fstat(fd,&st)!=0||!S_ISDIR(st.st_mode)||
    st.st_uid!=geteuid()||(st.st_mode&077)!=0){close(fd);return -1;}
 return fd;
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
  a->st_ctime==b->st_ctime;
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
 struct stat before,after;
 const int valid=fstat(fd,&before)==0&&S_ISREG(before.st_mode)&&
  before.st_nlink==1&&before.st_size==(off_t)MARK_SIZE&&
  read_exact(fd,contents,MARK_SIZE)&&fstat(fd,&after)==0&&
  same_file(&before,&after)&&
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
 close(fd);
 if(cancelled(cancel,ctx))ok=0;
 if(ok)ok=linkat(dir,temp,dir,name,0)==0;
 (void)unlinkat(dir,temp,0); /* staging only; never PS5 media */
 if(ok)ok=fsync(dir)==0;
 return ok;
}
static int raw_restored(int dir,const char*name,uint64_t size,
 const uint8_t expected[32],Cmp5CancelCheck cancel,void*ctx){
 int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 if(fd<0)return 0;
 struct stat before,after;
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
  same_file(&before,&after)&&!cancelled(cancel,ctx);
 close(fd);return ok;
}
int cmp5_verify_group(const char*workspace,const char*id,
 Cmp5GroupReceipt*r,Cmp5CancelCheck cancel,void*ctx){
 if(r)memset(r,0,sizeof(*r));
 if(!valid_id(id)||cancelled(cancel,ctx))return 0;
 int dir=safe_dir(workspace);
 if(dir<0)return 0;
 uint8_t marker[MARK_SIZE];Cmp5BackupReceipt part[CMP5_GROUP_MAX];
 Cmp5GroupReceipt found;uint32_t count=0;
 int ok=read_manifest(dir,id,0,marker,part,&count,&found);
 close(dir);
 for(unsigned i=0;ok&&i<count;i++){
  if(cancelled(cancel,ctx)){ok=0;break;}
  char name[48];member_name(name,id,i);
  Cmp5BackupReceipt actual;
  ok=cmp5_verify_backup(workspace,name,&actual,cancel,ctx)&&
   actual.bytes==part[i].bytes&&
   memcmp(actual.sha256,part[i].sha256,32)==0;
 }
 if(ok&&r)*r=found;
 return ok;
}
int cmp5_backup_group(const char*root,const Cmp5MediaCandidate*parts,
 uint32_t count,const char*workspace,const char*id,
 Cmp5GroupReceipt*r,Cmp5CancelCheck cancel,void*ctx){
 if(r)memset(r,0,sizeof(*r));
 if(!r||!valid_id(id)||!cmp5_validate_capture_group(parts,count)||
    cancelled(cancel,ctx))return 0;
 int dir=safe_dir(workspace);if(dir<0)return 0;
 char commit_name[48];marker_name(commit_name,id,0);
 struct stat st;
 int clear=fstatat(dir,commit_name,&st,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT;
 close(dir);
 if(!clear)return 0;
 Cmp5BackupReceipt pieces[CMP5_GROUP_MAX]={{0}};
 for(unsigned i=0;i<count;i++){
  if(cancelled(cancel,ctx))return 0;
  char name[48];member_name(name,id,i);
  if(!cmp5_backup_media(root,&parts[i],workspace,name,&pieces[i],cancel,ctx))
   return 0; /* Uncommitted components stay isolated, never reported ready. */
 }
 for(unsigned i=0;i<count;i++){
  char name[48];member_name(name,id,i);
  Cmp5BackupReceipt check;
  if(!cmp5_verify_backup(workspace,name,&check,cancel,ctx)||
     check.bytes!=pieces[i].bytes||
     memcmp(check.sha256,pieces[i].sha256,32)!=0)return 0;
 }
 uint8_t manifest[MARK_SIZE];manifest_header(manifest,pieces,count);
 dir=safe_dir(workspace);if(dir<0)return 0;
 int ok=publish_manifest(dir,id,0,manifest,cancel,ctx);
 close(dir);
 if(ok)ok=cmp5_verify_group(workspace,id,r,cancel,ctx);
 return ok;
}
int cmp5_verify_restored_group(const char*workspace,const char*id,
 Cmp5GroupReceipt*r,Cmp5CancelCheck cancel,void*ctx){
 if(r)memset(r,0,sizeof(*r));
 if(!valid_id(id)||cancelled(cancel,ctx))return 0;
 int dir=safe_dir(workspace);if(dir<0)return 0;
 uint8_t marker[MARK_SIZE];Cmp5BackupReceipt parts[CMP5_GROUP_MAX];
 Cmp5GroupReceipt result;uint32_t count=0;
 int ok=read_manifest(dir,id,1,marker,parts,&count,&result);
 for(unsigned i=0;ok&&i<count;i++){
  char name[48];member_name(name,id,i);
  ok=raw_restored(dir,name,parts[i].bytes,parts[i].sha256,cancel,ctx);
 }
 close(dir);if(ok&&r)*r=result;return ok;
}
int cmp5_restore_group(const char*backup,const char*id,const char*target,
 Cmp5GroupReceipt*r,Cmp5CancelCheck cancel,void*ctx){
 if(r)memset(r,0,sizeof(*r));
 if(!r||!valid_id(id)||!backup||!target||
    strcmp(backup,target)==0||cancelled(cancel,ctx))return 0;
 Cmp5GroupReceipt original;
 if(!cmp5_verify_group(backup,id,&original,cancel,ctx))return 0;
 int from=safe_dir(backup);if(from<0)return 0;
 uint8_t manifest[MARK_SIZE];Cmp5BackupReceipt pieces[CMP5_GROUP_MAX];
 uint32_t count=0;
 int ok=read_manifest(from,id,0,manifest,pieces,&count,NULL);
 close(from);if(!ok)return 0;
 int dest=safe_dir(target);
 if(dest<0)return 0;
 /* Any previous complete or partial restore with these IDs fails closed.
  * We NEVER remove an existing private file automatically. */
 char commit[48];marker_name(commit,id,1);
 struct stat st;
 ok=fstatat(dest,commit,&st,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT;
 for(unsigned i=0;ok&&i<count;i++){
  char name[48];member_name(name,id,i);
  ok=fstatat(dest,name,&st,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT;
 }
 close(dest);if(!ok)return 0;
 for(unsigned i=0;i<count;i++){
  if(cancelled(cancel,ctx))return 0;
  char name[48];member_name(name,id,i);
  Cmp5BackupReceipt receipt;
  if(!cmp5_restore_backup(backup,name,target,name,&receipt,cancel,ctx)||
     receipt.bytes!=pieces[i].bytes||
     memcmp(receipt.sha256,pieces[i].sha256,32)!=0)return 0;
 }
 dest=safe_dir(target);if(dest<0)return 0;
 for(unsigned i=0;ok&&i<count;i++){
  char name[48];member_name(name,id,i);
  ok=raw_restored(dest,name,pieces[i].bytes,pieces[i].sha256,cancel,ctx);
 }
 if(ok)ok=publish_manifest(dest,id,1,manifest,cancel,ctx);
 close(dest);
 if(ok)ok=cmp5_verify_restored_group(target,id,r,cancel,ctx);
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
 int duplicate=dup(dir);
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
 closedir(scan);
 return ok;
}
static int inspect_recovery(const char*workspace,const char*id,int restored,
 Cmp5RecoveryReport*report){
 if(!report)return 0;
 memset(report,0,sizeof(*report));
 if(!valid_id(id))return 0;
 int dir=safe_dir(workspace);
 if(dir<0)return 0;
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
 close(dir);
 if(!ok)return 0;
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
