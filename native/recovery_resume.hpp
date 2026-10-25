// Explicit AMPR checkpoint recovery after UI restart (PS5 host-tested only).
// This policy never reads checkpoint request bodies or authorizes deletion.
// The installed engine MUST independently validate sources/output inodes.
#pragma once
#include "compression_progress.hpp"
#include "compression_preflight.hpp"
#include "recovery_inventory.h"
#include <cstring>
#include <cstdio>

namespace cmp5_compression {
inline bool same_recovery_candidate(const Cmp5RecoveryCandidate&a,
                                    const Cmp5RecoveryCandidate&b) noexcept {
 return cmp5_recovery_title(a.title_id)&&
        cmp5_recovery_allowed_base(a.destination)&&
        (a.stage==CMP5_RECOVERY_PACK||a.stage==CMP5_RECOVERY_COPY||
         a.stage==CMP5_RECOVERY_VERIFY)&&
        std::strcmp(a.title_id,b.title_id)==0&&
        std::strcmp(a.destination,b.destination)==0&&a.stage==b.stage;
}
/* Reject a missing/swapped selected checkpoint. Partial coverage of a
 * DIFFERENT device does not invalidate a candidate retained by the
 * scanner's anchored, post-traversal revalidation on its own volume. */
template<typename Scanner>
bool recovery_candidate_fresh(const Cmp5RecoveryCandidate&selected,
                              Scanner scan) noexcept {
 Cmp5RecoveryInventory inventory{};
 if(!scan("/", &inventory)||inventory.count>CMP5_RECOVERY_MAX)return false;
 for(unsigned i=0;i<inventory.count;i++)
  if(same_recovery_candidate(selected,inventory.entries[i]))return true;
 return false;
}
/* A running/queued OTHER game must never be interrupted by a resumed job.
 * Idle may have no title ID after a native UI restart. */
inline bool recovery_engine_ready(const Cmp5RecoveryCandidate&candidate,
                                  const GameProgress&state) noexcept {
 if(!cmp5_recovery_title(candidate.title_id)||
    !cmp5_recovery_allowed_base(candidate.destination)||
    state.queue_len!=0u)return false;
 switch(state.state){
  case JobState::idle:return state.title_id[0]==0;
  case JobState::paused:
  case JobState::error:
  case JobState::cancelled:
   return std::strcmp(state.title_id,candidate.title_id)==0;
  default:return false;
 }
}
inline bool recovery_resume_body(const Cmp5RecoveryCandidate&selected,
                                 char*out,std::size_t cap) noexcept {
 if(!out||cap<32||!cmp5_recovery_title(selected.title_id)||
    !cmp5_recovery_allowed_base(selected.destination))return false;
 char base[192]{};
 const int n=std::snprintf(base,sizeof(base),"%s/%s",
                           selected.destination,selected.title_id);
 if(n<=0||static_cast<std::size_t>(n)>=sizeof(base))return false;
 char encoded[576]{};
 if(!form_encode_path(base,encoded,sizeof(encoded)))return false;
 const int count=std::snprintf(out,cap,"dest=%s&level=9",encoded);
 return count>0&&static_cast<std::size_t>(count)<cap;
}
} // namespace cmp5_compression
