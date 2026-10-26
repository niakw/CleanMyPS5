// Native group backups cover only VISIBLE, independently revalidated
// companion files, never undiscovered Gallery databases or Sony indexes.
// The group engine's restore functions are compiled OUT of the PS5 title.
#pragma once
#include "private_media_backup.hpp"
#include "verified_group.h"
#include <cstring>

namespace cmp5_private_backup {
enum class GroupStatus : unsigned {
 unattempted, refused, storage_unavailable, unsupported, failed, verified
};
/* Read-only, privacy-safe summary of PERSISTED original locators.
 * A matching original still does not validate Sony's separate indexes. */
struct SourceReview {
 bool checked=false;
 unsigned unchanged=0u,missing=0u,changed=0u,unknown=0u;
};
inline SourceReview summarize_sources(
 const Cmp5OriginLiveState states[CMP5_GROUP_MAX],
 unsigned count) noexcept {
 SourceReview summary{};
 if(!states||count<2u||count>CMP5_GROUP_MAX)return summary;
 for(unsigned i=0;i<count;i++){
  switch(states[i]){
   case CMP5_ORIGIN_UNCHANGED:++summary.unchanged;break;
   case CMP5_ORIGIN_MISSING:++summary.missing;break;
   case CMP5_ORIGIN_CHANGED:++summary.changed;break;
   case CMP5_ORIGIN_UNKNOWN:++summary.unknown;break;
   default:return SourceReview{}; // malformed state: do not claim a review
  }
 }
 summary.checked=true;
 return summary;
}
struct GroupOutcome {
 GroupStatus status=GroupStatus::unattempted;
 char id[32]{};
 Cmp5GroupReceipt receipt{};
 SourceReview sources{};
};
inline bool group_name(const Cmp5MediaCandidate&primary,char out[32]) noexcept {
 char capsule[32]{};
 if(!capsule_name(primary,capsule))return false;
 if(std::snprintf(out,32,"GRP_%s",capsule+4)!=20)return false;
 return true;
}
/* Old GROUP capsules can be checked after restart without consulting
 * the current Gallery. Valid group IDs are opaque uppercase hex only. */
inline GroupOutcome verify_saved_group(const char*id,
 Cmp5CancelCheck cancel,void*ctx) noexcept {
 GroupOutcome out{};
 if(!id||strnlen(id,21u)!=20u||std::memcmp(id,"GRP_",4u)!=0||
    (cancel&&cancel(ctx))){
  out.status=GroupStatus::refused;return out;
 }
 for(unsigned i=4u;i<20u;i++){
  const char value=id[i];
  if(!((value>='0'&&value<='9')||(value>='A'&&value<='F'))){
   out.status=GroupStatus::refused;return out;
  }
 }
 std::memcpy(out.id,id,21u);
 Cmp5GroupReceipt checked{};
 if(!cmp5_verify_group(workspace,id,&checked,cancel,ctx)||
    checked.count<2u||checked.count>CMP5_GROUP_MAX||
    !checked.bytes||checked.bytes>CMP5_BACKUP_LIMIT||
    (cancel&&cancel(ctx))){
  out.status=GroupStatus::failed;return out;
 }
 out.receipt=checked;
 out.status=GroupStatus::verified;
 /* An intact archive does not imply that the Gallery still contains the
  * originals. Scan the persistently saved locators after SHA verification;
  * unknown/unavailable inspection NEVER invalidates the archived bytes or
  * authorizes any write. The native worker handles cancellation. */
 Cmp5OriginLiveState states[CMP5_GROUP_MAX]{};
 if(cmp5_inspect_group_originals("/",workspace,id,states,cancel,ctx)&&
    !(cancel&&cancel(ctx)))
  out.sources=summarize_sources(states,checked.count);
 return out;
}
/* Two affirmative controller actions precede this call. This code cannot
 * request a restore or gallery mutation, and it never accepts an arbitrary
 * workspace or group ID from the controller. */
inline GroupOutcome backup_visible_group(const Cmp5MediaCandidate&primary,
 Cmp5CancelCheck cancel,void*ctx) noexcept {
 GroupOutcome out{};
 if(!group_name(primary,out.id)||(cancel&&cancel(ctx))){
  out.status=GroupStatus::refused;return out;
 }
 Cmp5MediaCandidate parts[CMP5_GROUP_MAX]{};
 uint32_t count=0;
 if(!cmp5_plan_capture_group("/",&primary,parts,&count,cancel,ctx)||
    count<2u||count>CMP5_GROUP_MAX||
    !cmp5_validate_capture_group(parts,count)||
    (cancel&&cancel(ctx))){
  out.status=GroupStatus::unsupported;
  std::memset(parts,0,sizeof(parts));
  return out;
 }
 if(!private_workspace_ready()){
  out.status=GroupStatus::storage_unavailable;
  std::memset(parts,0,sizeof(parts));
  return out;
 }
 Cmp5GroupReceipt first{},checked{};
 const bool ok=cmp5_backup_group("/",parts,count,workspace,out.id,
                                 &first,cancel,ctx)!=0&&
   cmp5_verify_group(workspace,out.id,&checked,cancel,ctx)!=0&&
   first.count==count&&checked.count==first.count&&
   first.bytes==checked.bytes&&
   std::memcmp(first.manifest_sha256,checked.manifest_sha256,32u)==0&&
   !(cancel&&cancel(ctx));
 std::memset(parts,0,sizeof(parts));
 if(!ok){out.status=GroupStatus::failed;return out;}
 out.receipt=checked;
 out.status=GroupStatus::verified;
 return out;
}
} // namespace cmp5_private_backup
