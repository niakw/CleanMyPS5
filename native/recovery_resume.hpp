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
/* Check both fixed-size strings are terminated BEFORE passing them to the
 * C scanner's string predicates. In particular, a stale UI selection must
 * never cause strlen/strcmp to read beyond Cmp5RecoveryCandidate. */
inline bool valid_recovery_candidate(const Cmp5RecoveryCandidate&c) noexcept {
 if(!std::memchr(c.title_id,0,sizeof(c.title_id))||
    !std::memchr(c.destination,0,sizeof(c.destination)))return false;
 return cmp5_recovery_title(c.title_id)&&
        cmp5_recovery_allowed_base(c.destination)&&
        (c.stage==CMP5_RECOVERY_PACK||c.stage==CMP5_RECOVERY_COPY||
         c.stage==CMP5_RECOVERY_VERIFY);
}
inline bool same_recovery_candidate(const Cmp5RecoveryCandidate&a,
                                    const Cmp5RecoveryCandidate&b) noexcept {
 return valid_recovery_candidate(a)&&valid_recovery_candidate(b)&&
        std::strcmp(a.title_id,b.title_id)==0&&
        std::strcmp(a.destination,b.destination)==0&&a.stage==b.stage;
}
/* A freshly scanned checkpoint must still describe the selected title,
 * storage volume AND stage. Another partially inaccessible device does
 * not invalidate a candidate revalidated on its own anchored volume. */
template<typename Scanner>
bool recovery_candidate_fresh(const Cmp5RecoveryCandidate&selected,
                              Scanner scan) noexcept {
 if(!valid_recovery_candidate(selected))return false;
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
 if(!valid_recovery_candidate(candidate)||state.queue_len!=0u)return false;
 switch(state.state){
  case JobState::idle:return state.title_id[0]==0;
  case JobState::paused:
  case JobState::error:
  case JobState::cancelled:
   return std::strcmp(state.title_id,candidate.title_id)==0;
  default:return false;
 }
}
/* Construct the request atomically: callers must never see/send a
 * truncated form on a failed preflight. No source is deleted on resume. */
inline bool recovery_resume_body(const Cmp5RecoveryCandidate&selected,
                                 char*out,std::size_t cap) noexcept {
 if(out&&cap)out[0]=0;
 if(!out||cap<32||!valid_recovery_candidate(selected))return false;
 char base[192]{};
 const int n=std::snprintf(base,sizeof(base),"%s/%s",
                           selected.destination,selected.title_id);
 if(n<=0||static_cast<std::size_t>(n)>=sizeof(base))return false;
 char encoded[576]{};
 if(!form_encode_path(base,encoded,sizeof(encoded)))return false;
 /* The pinned AMPR /api/resume endpoint reads an omitted 'level'
  * as 0, and job_resume_level(dest,0) preserves the checkpoint's LZ4
  * level. Supplying 9 silently changed a level 1..12 choice after a
  * title restart, violating the user's confirmed conversion plan. */
 char complete[600]{};
 const int count=std::snprintf(complete,sizeof(complete),
                               "dest=%s",encoded);
 if(count<=0||static_cast<std::size_t>(count)>=sizeof(complete)||
    static_cast<std::size_t>(count)>=cap)return false;
 std::memcpy(out,complete,static_cast<std::size_t>(count)+1u);
 return true;
}
} // namespace cmp5_compression
