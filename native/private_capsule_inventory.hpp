// Read-only inventory of native private backup capsules.
// Never enumerates Gallery paths or starts an implicit restore/delete.
#pragma once
#include "private_media_backup.hpp"
#include "verified_group.h"
#include "private_workspace_guard.h"
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cmp5_private_backup {
constexpr unsigned inventory_limit=32u, inventory_scan_limit=1024u;
struct Inventory {
 bool checked=false,partial=false;
 unsigned count=0u,staged=0u,group_count=0u;
 char ids[inventory_limit][32]{};
 char group_ids[inventory_limit][32]{};
};
/* User-triggered SHA-256 audit of all bounded private capsules/groups.
 * Counts are NOT proof that the underlying Gallery can be restored.
 * 'partial' includes undiscovered or interrupted private entries. */
struct ArchiveAudit {
 bool checked=false,partial=false;
 unsigned capsules=0u,valid_capsules=0u,groups=0u,valid_groups=0u;
 std::uint64_t verified_bytes=0u; // per-archive sum, may include duplicates
};
inline bool same_archive_listing(const Inventory&a,const Inventory&b) noexcept {
 if(!a.checked||!b.checked||a.partial!=b.partial||
    a.staged!=b.staged||a.count!=b.count||
    a.group_count!=b.group_count)return false;
 /* readdir order is not stable; compare IDs as bounded SETS. */
 for(unsigned i=0;i<a.count;i++){
  bool found=false;
  for(unsigned j=0;j<b.count;j++)
   if(std::strcmp(a.ids[i],b.ids[j])==0){found=true;break;}
  if(!found)return false;
 }
 for(unsigned i=0;i<a.group_count;i++){
  bool found=false;
  for(unsigned j=0;j<b.group_count;j++)
   if(std::strcmp(a.group_ids[i],b.group_ids[j])==0){found=true;break;}
  if(!found)return false;
 }
 return true;
}
/* A testable worker phase: verify every ID independently and fail closed
 * on cancellation. Invalid checks are counted, never hidden or deleted. */
inline bool audit_inventory_at(const Inventory&listed,const char*folder,
                               ArchiveAudit*out,Cmp5CancelCheck cancel,
                               void*context) noexcept {
 if(out)*out=ArchiveAudit{};
 if(!out||!folder||!listed.checked||
    listed.count>inventory_limit||listed.group_count>inventory_limit||
    (cancel&&cancel(context)))return false;
 ArchiveAudit fresh{};
 fresh.partial=listed.partial;
 for(unsigned i=0;i<listed.count;i++){
  if(cancel&&cancel(context))return false;
  if(!valid_capsule_id(listed.ids[i]))return false;
  Cmp5BackupReceipt receipt{};
  ++fresh.capsules;
  if(cmp5_verify_backup(folder,listed.ids[i],&receipt,cancel,context)&&
     receipt.published&&receipt.bytes>0u&&
     receipt.bytes<=CMP5_BACKUP_LIMIT){
   ++fresh.valid_capsules;
   fresh.verified_bytes=(UINT64_MAX-fresh.verified_bytes<receipt.bytes)?
     UINT64_MAX:fresh.verified_bytes+receipt.bytes;
  }
 }
 for(unsigned i=0;i<listed.group_count;i++){
  if(cancel&&cancel(context))return false;
  if(!valid_group_id(listed.group_ids[i]))return false;
  Cmp5GroupReceipt receipt{};
  ++fresh.groups;
  if(cmp5_verify_group(folder,listed.group_ids[i],&receipt,cancel,context)&&
     receipt.count>=2u&&receipt.count<=CMP5_GROUP_MAX&&
     receipt.bytes>0u&&receipt.bytes<=CMP5_BACKUP_LIMIT){
   ++fresh.valid_groups;
   fresh.verified_bytes=(UINT64_MAX-fresh.verified_bytes<receipt.bytes)?
     UINT64_MAX:fresh.verified_bytes+receipt.bytes;
  }
 }
 if(cancel&&cancel(context))return false;
 fresh.checked=true;
 *out=fresh;
 return true;
}
/* The directory inode can remain stable while files are added, removed,
 * renamed or replaced. Do not publish a "complete" stale inventory when
 * its directory metadata changed during traversal. This is an observation
 * fence, not an atomic snapshot or authorisation to restore Gallery files. */
inline bool same_inventory_directory(const struct stat&a,
                                     const struct stat&b) noexcept {
#if defined(__APPLE__)
 const bool nano=a.st_mtimespec.tv_nsec==b.st_mtimespec.tv_nsec&&
                 a.st_ctimespec.tv_nsec==b.st_ctimespec.tv_nsec;
#else
 const bool nano=a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&
                 a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
#endif
 return nano&&S_ISDIR(a.st_mode)&&S_ISDIR(b.st_mode)&&
   a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&
   a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_gid==b.st_gid&&
   a.st_nlink==b.st_nlink&&a.st_size==b.st_size&&
   a.st_mtime==b.st_mtime&&a.st_ctime==b.st_ctime;
}
inline bool valid_capsule_id(const char*id) noexcept {
 if(!id||strnlen(id,21u)!=20u||std::memcmp(id,"CAP_",4u)!=0)return false;
 for(unsigned i=4u;i<20u;i++){
  const char c=id[i];
  if(!((c>='0'&&c<='9')||(c>='A'&&c<='F')))return false;
 }
 return true;
}
/* Discover a committed group by a bounded, name-only marker. Presence
 * is NOT proof: cmp5_verify_group must rehash all members on selection. */
inline bool valid_group_id(const char*id) noexcept {
 if(!id||strnlen(id,21u)!=20u||std::memcmp(id,"GRP_",4u)!=0)return false;
 for(unsigned i=4u;i<20u;i++){
  const char v=id[i];
  if(!((v>='0'&&v<='9')||(v>='A'&&v<='F')))return false;
 }
 return true;
}
inline bool group_entry_name(const char*name,bool*commit,
                             bool*temporary) noexcept {
 if(commit)*commit=false;
 if(temporary)*temporary=false;
 if(!name)return false;
 const std::size_t len=strnlen(name,40u);
 if(len>=40u)return false;
 const bool stage=name[0]=='.';
 if(len<(stage?21u:20u))return false;
 const char*base=name+(stage?1:0);
 char id[21]{};
 std::memcpy(id,base,20u);
 if(!valid_group_id(id))return false;
 const char*suffix=base+20u;
 if(!stage){
  if(std::strcmp(suffix,"__COMMIT")==0){
   if(commit)*commit=true;
   return true;
  }
  return std::strcmp(suffix,"__1")==0||
         std::strcmp(suffix,"__2")==0||
         std::strcmp(suffix,"__3")==0||
         std::strcmp(suffix,"__4")==0||
         std::strcmp(suffix,"__ORIGINS")==0;
 }
 const std::size_t suffixlen=std::strlen(suffix);
 if(suffixlen<=5u||suffixlen>=24u||
    std::strcmp(suffix+suffixlen-5u,".part")!=0)return false;
 char trimmed[24]{};
 std::memcpy(trimmed,suffix,suffixlen-5u);
 if(std::strcmp(trimmed,"__COMMIT")!=0&&
    std::strcmp(trimmed,"__1")!=0&&
    std::strcmp(trimmed,"__2")!=0&&
    std::strcmp(trimmed,"__3")!=0&&
    std::strcmp(trimmed,"__4")!=0&&
    std::strcmp(trimmed,"__ORIGINS")!=0)return false;
 if(temporary)*temporary=true;
 return true;
}
/* Use a pinned descriptor of a 0700 private directory. Listing is only
 * advisory; selecting an item must invoke cmp5_verify_backup, which opens
 * and independently hashes the currently named capsule. */
inline bool scan_at(const char*folder,Inventory*out,
                    Cmp5CancelCheck cancel,void*context) noexcept {
 if(out)*out=Inventory{};
 if(!folder||!out||(cancel&&cancel(context)))return false;
 const int fd=cmp5_open_private_workspace(folder);
 if(fd<0)return false;
 struct stat anchor{};
 if(fstat(fd,&anchor)!=0||!S_ISDIR(anchor.st_mode)){
  close(fd);return false;
 }
 /* PS5 libc does not export dirfd(): keep a separately owned anchor
  * for fstatat while fdopendir owns a duplicate for traversal. */
 const int iter=dup(fd);
 if(iter<0){close(fd);return false;}
 DIR*stream=fdopendir(iter);
 if(!stream){close(iter);close(fd);return false;}
 Inventory found{};
 unsigned observed=0u;
 bool good=true;
 for(;;){
  if(cancel&&cancel(context)){good=false;break;}
  errno=0;
  struct dirent*entry=readdir(stream);
  if(!entry){
   if(errno)good=false;
   break;
  }
  if(std::strcmp(entry->d_name,".")==0||
     std::strcmp(entry->d_name,"..")==0)continue;
  if(++observed>inventory_scan_limit){found.partial=true;break;}
  /* Group COMMIT/ORIGINS staging names can be 34-35 bytes.
   * A 32-byte bound incorrectly discarded them without incrementing the
   * interrupted-transaction count. Names >= 40 remain untrusted. */
  const std::size_t len=strnlen(entry->d_name,40u);
  if(len==40u){found.partial=true;continue;}
  const bool is_capsule=valid_capsule_id(entry->d_name);
  char staged_id[21]{};
  if(len==26u&&entry->d_name[0]=='.')
   std::memcpy(staged_id,entry->d_name+1,20u);
  const bool is_stage=len==26u&&entry->d_name[0]=='.'&&
   valid_capsule_id(staged_id)&&
   std::strcmp(entry->d_name+21,".part")==0;
  if(is_stage){
   ++found.staged;found.partial=true;continue;
  }
  if(!is_capsule){
   bool commit=false,temporary=false;
   if(group_entry_name(entry->d_name,&commit,&temporary)){
    if(temporary){++found.staged;found.partial=true;continue;}
    struct stat st{};
    if(fstatat(fd,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)!=0||
       !S_ISREG(st.st_mode)||st.st_uid!=geteuid()||
       st.st_nlink!=1||(st.st_mode&0077)!=0||
       (commit&&st.st_size!=256)||
       (!commit&&(st.st_size<=0||
             st.st_size>(off_t)(CMP5_BACKUP_LIMIT+64u)))){
     found.partial=true;continue;
    }
    if(commit){
     if(found.group_count==inventory_limit){found.partial=true;continue;}
     std::memcpy(found.group_ids[found.group_count],
                 entry->d_name,20u);
     ++found.group_count;
    }
    continue;
   }
   found.partial=true;continue;
  }
  struct stat st{};
  if(fstatat(fd,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)!=0||
     !S_ISREG(st.st_mode)||st.st_uid!=geteuid()||
     st.st_nlink!=1||(st.st_mode&0077)!=0||
     st.st_size<=64||st.st_size>(off_t)(CMP5_BACKUP_LIMIT+64u)){
   found.partial=true;
   continue;
  }
  if(found.count==inventory_limit){found.partial=true;continue;}
  std::memcpy(found.ids[found.count],entry->d_name,len+1u);
  ++found.count;
 }
 if(closedir(stream)!=0)good=false;
 if(cancel&&cancel(context))good=false;
 /* A pinned fd stays readable after a directory is renamed or replaced.
  * Reopen the original workspace pathname using the full 0700/private
  * guard and compare identities before publishing ANY catalogue entry.
  * An obsolete listing is not a valid post-restart archive inventory.
  * Verification of a selected capsule still reopens and rehashes it. */
 const int current=good?cmp5_open_private_workspace(folder):-1;
 struct stat held_after{},fresh{};
 if(current<0||fstat(fd,&held_after)!=0||
    fstat(current,&fresh)!=0||
    !same_inventory_directory(anchor,held_after)||
    !same_inventory_directory(anchor,fresh))good=false;
 if(current>=0)close(current);
 close(fd);
 if(!good)return false;
 found.checked=true;
 *out=found;
 return true;
}
inline bool scan(Inventory*out,Cmp5CancelCheck cancel,void*context) noexcept {
 return scan_at(workspace,out,cancel,context);
}
/* Refresh the list before AND after hashing. A changed set of names
 * cannot be presented as a complete audit; checks are read-only, and
 * even two matching snapshots do not make Sony's index state atomic. */
inline bool audit_at(const char*folder,ArchiveAudit*out,
                     Cmp5CancelCheck cancel,void*context) noexcept {
 if(out)*out=ArchiveAudit{};
 if(!folder||!out||(cancel&&cancel(context)))return false;
 Inventory before{},after{};
 ArchiveAudit result{};
 if(!scan_at(folder,&before,cancel,context)||
    !audit_inventory_at(before,folder,&result,cancel,context)||
    !scan_at(folder,&after,cancel,context)||
    !same_archive_listing(before,after)||
    (cancel&&cancel(context)))return false;
 *out=result;
 return true;
}
inline bool audit(ArchiveAudit*out,Cmp5CancelCheck cancel,
                  void*context) noexcept {
 return audit_at(workspace,out,cancel,context);
}
inline Outcome verify_by_id(const char*id,Cmp5CancelCheck cancel,
                            void*context) noexcept {
 Outcome out{};
 if(!valid_capsule_id(id)||(cancel&&cancel(context))){
  out.status=Result::denied;return out;
 }
 std::memcpy(out.capsule_id,id,21u);
 Cmp5BackupReceipt receipt{};
 if(!cmp5_verify_backup(workspace,id,&receipt,cancel,context)||
    !receipt.published||!receipt.bytes||receipt.bytes>CMP5_BACKUP_LIMIT||
    (cancel&&cancel(context))){
  out.status=Result::verification_failed;return out;
 }
 out.status=Result::verified;
 out.bytes=receipt.bytes;
 return out;
}
} // namespace cmp5_private_backup
