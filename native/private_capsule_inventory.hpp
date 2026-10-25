// Read-only inventory of native private backup capsules.
// Never enumerates Gallery paths or starts an implicit restore/delete.
#pragma once
#include "private_media_backup.hpp"
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
 unsigned count=0u,staged=0u;
 char ids[inventory_limit][32]{};
};
inline bool valid_capsule_id(const char*id) noexcept {
 if(!id||std::strlen(id)!=20u||std::memcmp(id,"CAP_",4u)!=0)return false;
 for(unsigned i=4u;i<20u;i++){
  const char c=id[i];
  if(!((c>='0'&&c<='9')||(c>='A'&&c<='F')))return false;
 }
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
  const std::size_t len=strnlen(entry->d_name,32u);
  if(len==32u){found.partial=true;continue;}
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
  if(!is_capsule){found.partial=true;continue;}
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
 close(fd);
 if(!good)return false;
 found.checked=true;
 *out=found;
 return true;
}
inline bool scan(Inventory*out,Cmp5CancelCheck cancel,void*context) noexcept {
 return scan_at(workspace,out,cancel,context);
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
