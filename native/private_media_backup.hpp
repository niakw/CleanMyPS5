// Native, additive-only media backup. The only writable location is the
// app's own private /download0 workspace. The Gallery is opened READ ONLY.
// This is not a Gallery/index restore and never enables media deletion.
#pragma once
#include "verified_backup.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cmp5_private_backup {
constexpr const char* workspace="/download0/cmp5-private-backups";
enum class Result : unsigned {
 unattempted, denied, source_unavailable, storage_unavailable,
 verification_failed, verified
};
struct Outcome {
 Result status=Result::unattempted;
 char capsule_id[32]{}; // opaque local identifier, never a source path
 std::uint64_t bytes=0;
};
/* Unpredictability isn't relied upon: this is a no-clobber storage label,
 * not an authorization token. Keep source names OUT of title logs/UI. */
inline bool capsule_name(const Cmp5MediaCandidate&entry,
                         char output[32]) noexcept {
 const std::size_t n=strnlen(entry.preview_path,sizeof(entry.preview_path));
 if(n<32||n>=sizeof(entry.preview_path)||entry.preview_path[0]!='/'||
    (entry.media_type!=1u&&entry.media_type!=2u)||
    entry.bytes==0||entry.bytes>CMP5_BACKUP_LIMIT||
    !entry.snapshot_device||!entry.snapshot_inode||
    entry.modified_at<=0||entry.snapshot_ctime<=0)return false;
 std::uint64_t digest=UINT64_C(14695981039346656037);
 for(std::size_t i=0;i<n;i++){
  digest^=static_cast<unsigned char>(entry.preview_path[i]);
  digest*=UINT64_C(1099511628211);
 }
 const std::uint64_t values[]={entry.bytes,entry.snapshot_device,
    entry.snapshot_inode,static_cast<std::uint64_t>(entry.modified_at),
    static_cast<std::uint64_t>(entry.snapshot_ctime),
    entry.snapshot_mtime_nsec,entry.snapshot_ctime_nsec};
 for(const auto value:values)
  for(unsigned i=0;i<8u;i++){
   digest^=static_cast<unsigned char>(value>>(i*8u));
   digest*=UINT64_C(1099511628211);
  }
 return std::snprintf(output,32,"CAP_%016llX",
                      static_cast<unsigned long long>(digest))==20;
}
inline bool private_workspace_ready() noexcept {
 const int root=open("/download0",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(root<0)return false;
 struct stat base{};
 bool ok=fstat(root,&base)==0&&S_ISDIR(base.st_mode);
 if(ok&&mkdirat(root,"cmp5-private-backups",0700)!=0&&errno!=EEXIST)
  ok=false;
 int folder=-1;
 if(ok)folder=openat(root,"cmp5-private-backups",
                      O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 struct stat actual{},named{};
 if(ok)ok=folder>=0&&
   fstat(folder,&actual)==0&&
   fstatat(root,"cmp5-private-backups",&named,AT_SYMLINK_NOFOLLOW)==0&&
   S_ISDIR(actual.st_mode)&&S_ISDIR(named.st_mode)&&
   actual.st_dev==named.st_dev&&actual.st_ino==named.st_ino&&
   actual.st_uid==geteuid()&&
   (actual.st_mode&0777)==0700;
 if(folder>=0)close(folder);
 close(root);
 return ok;
}
/* Read-only post-restart check of the *currently selected source version*.
 * Do not call private_workspace_ready(): that helper may create a folder.
 * A new source inode/timestamp produces a different capsule label, so a
 * historical backup is not falsely described as this version's backup.
 * Capsule integrity alone does not establish Sony's Gallery index safety. */
inline Outcome verify_existing(const Cmp5MediaCandidate&item,
                               Cmp5CancelCheck cancel,
                               void*context) noexcept {
 Outcome out{};
 if(!capsule_name(item,out.capsule_id)){
  out.status=Result::denied;return out;
 }
 if(cancel&&cancel(context)){
  out.status=Result::denied;return out;
 }
 Cmp5BackupReceipt capsule{};
 if(!cmp5_verify_backup(workspace,out.capsule_id,&capsule,
                        cancel,context)||!capsule.published||
    capsule.bytes!=item.bytes||(cancel&&cancel(context))){
  out.status=Result::verification_failed;return out;
 }
 out.status=Result::verified;
 out.bytes=capsule.bytes;
 return out;
}
/* Only one Cmp5MediaCandidate from a successful native scan may be passed.
 * We revalidate its inode and timestamps inside cmp5_backup_media().
 * No alternate workspace or source path is accepted from the UI. */
inline Outcome backup_one(const Cmp5MediaCandidate&item,
                          Cmp5CancelCheck cancel,void*context) noexcept {
 Outcome out{};
 if(!capsule_name(item,out.capsule_id)){out.status=Result::denied;return out;}
 if(cancel&&cancel(context)){out.status=Result::denied;return out;}
 if(!private_workspace_ready()){
  out.status=Result::storage_unavailable;return out;
 }
 Cmp5BackupReceipt receipt{};
 if(!cmp5_backup_media("/",&item,workspace,out.capsule_id,
                       &receipt,cancel,context)){
  out.status=Result::source_unavailable;return out;
 }
 /* backup_media does a full second-pass SHA-256 verification. Keep a
  * third independently opened check here before claiming native success. */
 Cmp5BackupReceipt checked{};
 if(!cmp5_verify_backup(workspace,out.capsule_id,&checked,cancel,context)||
    !checked.published||checked.bytes!=receipt.bytes||
    std::memcmp(checked.sha256,receipt.sha256,32)!=0){
  out.status=Result::verification_failed;return out;
 }
 out.bytes=checked.bytes;
 out.status=Result::verified;
 return out;
}
} // namespace cmp5_private_backup
