/* CleanMyPS5 / GPL-3.0-or-later. Canonical fingerprint of AMPR's
 * FINAL source-file action plan, after profile rules and block budgeting.
 * Requires pinned engine scan.h / SHA256 implementation.  The header is
 * compiled in BOTH the PS5 worker and the host regression fixture. */
#ifndef CMP5_AMPR_CHECKPOINT_PLAN_H
#define CMP5_AMPR_CHECKPOINT_PLAN_H
#include "scan.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* AMPR's host-only CLI fixtures do not use the PS5 resume fingerprint.
 * The separate SHA regression fixture builds this without E2A_HOST. */
#ifndef E2A_HOST
static void cmp5_plan_hash_u64(e2a_sha256 *hash,uint64_t value){
  unsigned char little[8];
  for(unsigned i=0;i<8u;i++)little[i]=(unsigned char)(value>>(i*8u));
  e2a_sha256_update(hash,little,sizeof little);
}
static int cmp5_entries_plan_hash(const e2a_list *entries,char hex[65]){
  if(!entries||!hex||(entries->n && !entries->v))return -1;
  e2a_sha256 hash;
  e2a_sha256_init(&hash);
  static const char tag[]="CMP5-FILE-PLAN-v1";
  e2a_sha256_update(&hash,tag,sizeof tag);
  cmp5_plan_hash_u64(&hash,(uint64_t)entries->n);
  for(size_t i=0;i<entries->n;i++){
    const e2a_entry *item=&entries->v[i];
    if(!item->rel)return -1;
    const size_t len=strlen(item->rel);
    if(len>65535u)return -1;
    cmp5_plan_hash_u64(&hash,(uint64_t)len);
    e2a_sha256_update(&hash,item->rel,len);
    cmp5_plan_hash_u64(&hash,item->size);
    /* Synthetic runtime files are reinstalled and may obtain a fresh
     * mtime. Their filenames, sizes and actions still belong to the plan. */
    cmp5_plan_hash_u64(&hash,item->synthetic?0u:(uint64_t)item->mtime);
    unsigned char flags[5]={item->action,item->block_shift,
      item->layout,(unsigned char)item->hot,
      (unsigned char)item->synthetic};
    e2a_sha256_update(&hash,flags,sizeof flags);
  }
  unsigned char digest[32];
  e2a_sha256_final(&hash,digest);
  static const char chars[]="0123456789abcdef";
  for(size_t i=0;i<32u;i++){
    hex[2u*i]=chars[digest[i]>>4];
    hex[2u*i+1u]=chars[digest[i]&15u];
  }
  hex[64]=0;
  return 0;
}
/* V2: bind the ACTUAL BYTES of every synthetic runtime/plugin to the
 * finalized action plan. Earlier v1 covered size/mtime (synthetic mtime
 * intentionally ignored), so a same-size plugin replacement could pass.
 * This helper reads through a no-follow fd; it never creates or repairs
 * any file. The old v1 routine remains only for compatibility tests. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
/* Keep the exact runtime/plugin directory in the live namespace, not
 * merely the inode reached at the beginning of a potentially long hash. */
static int cmp5_open_synthetic_assets(const char*destination){
  if(!destination||destination[0]!='/')return -1;
  int parent=open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
  if(parent<0)return -1;
  const char*part=destination+1;
  if(!*part){close(parent);return -1;}
  while(*part){
    const char*slash=strchr(part,'/');
    const size_t len=slash?(size_t)(slash-part):strlen(part);
    if(len==0u||len>255u||(len==1u&&part[0]=='.')||
       (len==2u&&part[0]=='.'&&part[1]=='.')){
      close(parent);return -1;
    }
    char name[256];
    memcpy(name,part,len);name[len]=0;
    int next=openat(parent,name,O_RDONLY|O_DIRECTORY|
                    O_NOFOLLOW|O_CLOEXEC);
    close(parent);
    if(next<0)return -1;
    parent=next;
    if(!slash)break;
    part=slash+1;
    if(!*part){close(parent);return -1;}
  }
  int assets=openat(parent,"fakelib",O_RDONLY|O_DIRECTORY|
                    O_NOFOLLOW|O_CLOEXEC);
  close(parent);
  return assets;
}
static int cmp5_same_asset_directory(const struct stat*a,const struct stat*b){
  return a&&b&&S_ISDIR(a->st_mode)&&S_ISDIR(b->st_mode)&&
         a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
         a->st_mode==b->st_mode&&a->st_nlink==b->st_nlink&&
         a->st_uid==b->st_uid&&a->st_gid==b->st_gid&&
         a->st_size==b->st_size&&
         a->st_mtime==b->st_mtime&&a->st_ctime==b->st_ctime;
}
static int cmp5_asset_directory_current(const char*destination,
                                         int assets,
                                         const struct stat*before){
  struct stat held={0},named={0};
  if(assets<0||!before||fstat(assets,&held)!=0||
     !cmp5_same_asset_directory(before,&held))return 0;
  const int again=cmp5_open_synthetic_assets(destination);
  if(again<0)return 0;
  const int ok=fstat(again,&named)==0&&
               cmp5_same_asset_directory(before,&named);
  close(again);
  return ok;
}
static int cmp5_hash_synthetic_file(const char*destination,
                                    const e2a_entry*entry,
                                    unsigned char digest[32]){
  if(!destination||!entry||!entry->rel||!digest||
     entry->size==0u||entry->size>64u*1024u*1024u)return -1;
  /* Only recognized, directly published fakelib artefacts may be read.
   * Refuse relative path traversal and nested/arbitrary trees. */
  if(strncmp(entry->rel,"fakelib/",8u)!=0)return -1;
  const char*leaf=entry->rel+8u;
  const size_t leaflen=strlen(leaf);
  if(!leaflen||leaf[0]=='.'||leaflen>120u||
     strstr(leaf,"..")||strchr(leaf,'/')||strchr(leaf,'\\'))return -1;
  for(size_t i=0;i<leaflen;i++){
    const unsigned char c=(unsigned char)leaf[i];
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
         (c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return -1;
  }
  /* Resolve each ancestor without following symlinks. Re-open the
   * full namespace after hashing: a held fd can outlive a rename. */
  int assets=cmp5_open_synthetic_assets(destination);
  if(assets<0)return -1;
  struct stat parent_before={0};
  if(fstat(assets,&parent_before)!=0){
    close(assets);return -1;
  }
  int fd=openat(assets,leaf,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
  if(fd<0){close(assets);return -1;}
  struct stat before,after,named;
  int result=-1;
  if(fstat(fd,&before)!=0||!S_ISREG(before.st_mode)||
     before.st_nlink!=1||before.st_size<0||
     (uint64_t)before.st_size!=entry->size)goto complete;
  e2a_sha256 hash;
  e2a_sha256_init(&hash);
  uint8_t data[8192];
  uint64_t bytes=0u;
  for(;;){
    const ssize_t n=read(fd,data,sizeof(data));
    if(n<0&&errno==EINTR)continue;
    if(n<0)goto complete;
    if(n==0)break;
    bytes+=(uint64_t)n;
    if(bytes>entry->size)goto complete;
    e2a_sha256_update(&hash,data,(size_t)n);
  }
  if(bytes!=entry->size||fstat(fd,&after)!=0||
     before.st_dev!=after.st_dev||before.st_ino!=after.st_ino||
     before.st_size!=after.st_size||before.st_mode!=after.st_mode||
     before.st_mtime!=after.st_mtime||
     before.st_ctime!=after.st_ctime||
     fstatat(assets,leaf,&named,AT_SYMLINK_NOFOLLOW)!=0||
     !S_ISREG(named.st_mode)||named.st_nlink!=1||
     named.st_dev!=after.st_dev||named.st_ino!=after.st_ino||
     named.st_size!=after.st_size||named.st_mode!=after.st_mode||
     named.st_mtime!=after.st_mtime||
     named.st_ctime!=after.st_ctime||
     !cmp5_asset_directory_current(destination,assets,&parent_before))
    goto complete;
  /* Confirm the named file through the re-resolved directory as well;
   * a valid old directory inode is not a live output destination. */
  int current_assets=cmp5_open_synthetic_assets(destination);
  if(current_assets<0)goto complete;
  struct stat live={0};
  const int live_ok=fstatat(current_assets,leaf,&live,AT_SYMLINK_NOFOLLOW)==0&&
     S_ISREG(live.st_mode)&&live.st_nlink==1&&
     live.st_dev==after.st_dev&&live.st_ino==after.st_ino&&
     live.st_size==after.st_size&&live.st_mode==after.st_mode&&
     live.st_mtime==after.st_mtime&&live.st_ctime==after.st_ctime;
  close(current_assets);
  if(!live_ok)goto complete;
  e2a_sha256_final(&hash,digest);
  result=0;
complete:
  close(fd);
  close(assets);
  return result;
}
static int cmp5_entries_plan_hash_with_assets(const e2a_list*entries,
                                               const char*destination,
                                               char out[65]){
  if(!entries||!destination||!out)return -1;
  char plan_hex[65];
  if(cmp5_entries_plan_hash(entries,plan_hex)!=0)return -1;
  e2a_sha256 hash;
  e2a_sha256_init(&hash);
  static const char tag[]="CMP5-FILE-PLAN-ASSETS-v2";
  e2a_sha256_update(&hash,tag,sizeof(tag));
  e2a_sha256_update(&hash,plan_hex,64u);
  uint64_t asset_count=0u;
  for(size_t i=0;i<entries->n;i++){
    const e2a_entry*entry=&entries->v[i];
    if(!entry->synthetic)continue;
    unsigned char asset_sha[32];
    if(cmp5_hash_synthetic_file(destination,entry,asset_sha)!=0)return -1;
    ++asset_count;
    cmp5_plan_hash_u64(&hash,(uint64_t)i);
    e2a_sha256_update(&hash,asset_sha,sizeof(asset_sha));
  }
  cmp5_plan_hash_u64(&hash,asset_count);
  unsigned char digest[32];
  e2a_sha256_final(&hash,digest);
  static const char chars[]="0123456789abcdef";
  for(size_t i=0;i<32u;i++){
    out[i*2u]=chars[digest[i]>>4u];
    out[i*2u+1u]=chars[digest[i]&15u];
  }
  out[64]=0;
  return 0;
}
#endif /* !E2A_HOST */
#endif
