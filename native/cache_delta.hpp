// Local cache shortlist delta: observation only, never evidence of idleness.
// Only compares successful /data cache inventories from this title process.
// GPL-3.0-or-later.
#pragma once
#include "homebrew.h"
#include <cstdint>
#include <cstring>
namespace cmp5_cache_delta {
enum class State : unsigned { no_baseline, incomplete, compared };
enum class Kind : unsigned { incomparable, changed, unchanged_observed };
struct Entry {
 char app_label[48]{};
 unsigned kind=0;
 Kind result=Kind::incomparable;
 std::uint64_t old_bytes=0,new_bytes=0;
 std::uint32_t old_files=0,new_files=0;
 bool metadata_compared=false;
};
struct Comparison {
 State state=State::no_baseline;
 unsigned compared=0,changed=0,unchanged_observed=0,incomparable=0;
 Entry entries[CMP5_CACHE_TOP_CAP]{};
 bool idle_verified=false;
 bool deletion_allowed=false;
};
inline bool complete(const Cmp5HomebrewStats&s) noexcept {
 if(s.data_root_state!=1u||s.cache_like_partial!=0u||
    s.cache_ledger_overflow!=0u||
    s.cache_top_count>CMP5_CACHE_TOP_CAP||
    s.cache_ledger_count>CMP5_CACHE_LEDGER_CAP||
    s.cache_ledger_count!=s.cache_like_folders)return false;
 for(unsigned i=0;i<s.cache_top_count;i++){
  const void*end=std::memchr(s.cache_top[i].app_label,0,
                             sizeof(s.cache_top[i].app_label));
  if(s.cache_top[i].partial||!end||
     static_cast<const char*>(end)==s.cache_top[i].app_label||
     static_cast<const char*>(end)-s.cache_top[i].app_label>=47||
     !s.cache_top[i].identity_key)return false;
 }
 for(unsigned i=0;i<s.cache_ledger_count;i++)
  if(!s.cache_ledger[i].key)return false;
 return true;
}
inline unsigned matches(const Cmp5HomebrewStats&s,
                        std::uint64_t key) noexcept {
 unsigned n=0;
 for(unsigned i=0;i<s.cache_ledger_count;i++)
  if(s.cache_ledger[i].key==key)n++;
 return n;
}
inline const Cmp5CacheLedger*locate(const Cmp5HomebrewStats&s,
                                   std::uint64_t key) noexcept {
 for(unsigned i=0;i<s.cache_ledger_count;i++)
  if(s.cache_ledger[i].key==key)return &s.cache_ledger[i];
 return nullptr;
}
inline bool changed(const Cmp5CacheLedger&a,
                    const Cmp5CacheLedger&b) noexcept {
 return a.nominal_bytes!=b.nominal_bytes||a.files!=b.files||
        a.metadata_nodes!=b.metadata_nodes||
        a.metadata_xor!=b.metadata_xor||a.metadata_sum!=b.metadata_sum;
}
inline Comparison compare(const Cmp5HomebrewStats&before,
                          const Cmp5HomebrewStats&after,
                          bool has_baseline) noexcept {
 Comparison result{};
 if(!has_baseline)return result;
 if(!complete(before)||!complete(after)){
  result.state=State::incomplete;return result;
 }
 result.state=State::compared;
 // Count ALL observed cache folders, not just the four display previews.
 for(unsigned i=0;i<after.cache_ledger_count;i++){
  const auto&candidate=after.cache_ledger[i];
  if(matches(before,candidate.key)!=1u||
     matches(after,candidate.key)!=1u){
   result.incomparable++;continue;
  }
  const auto*prior=locate(before,candidate.key);
  if(!prior||!candidate.metadata_complete||!prior->metadata_complete||
     !candidate.metadata_nodes||!prior->metadata_nodes){
   result.incomparable++;continue;
  }
  result.compared++;
  if(changed(*prior,candidate))result.changed++;
  else result.unchanged_observed++;
 }
 // Previously observed directories absent this time are NOT declared
 // deleted or idle. Report the lost comparison coverage explicitly.
 for(unsigned i=0;i<before.cache_ledger_count;i++)
  if(matches(after,before.cache_ledger[i].key)==0u)
   result.incomparable++;
 // Four human-readable previews, with matching based on exact raw-name
 // keys instead of sanitised or truncated presentation labels.
 for(unsigned i=0;i<after.cache_top_count;i++){
  const auto&candidate=after.cache_top[i];
  auto&entry=result.entries[i];
  std::memcpy(entry.app_label,candidate.app_label,
              sizeof(entry.app_label));
  entry.kind=candidate.kind;
  entry.new_bytes=candidate.nominal_bytes;
  entry.new_files=candidate.files;
  if(matches(before,candidate.identity_key)!=1u||
     matches(after,candidate.identity_key)!=1u)continue;
  const auto*prior=locate(before,candidate.identity_key);
  const auto*current=locate(after,candidate.identity_key);
  if(!prior||!current||!prior->metadata_complete||
     !current->metadata_complete||!prior->metadata_nodes||
     !current->metadata_nodes)continue;
  entry.old_bytes=prior->nominal_bytes;
  entry.old_files=prior->files;
  entry.metadata_compared=true;
  entry.result=changed(*prior,*current)?
    Kind::changed:Kind::unchanged_observed;
 }
 return result;
}
static_assert(!Comparison{}.idle_verified);
static_assert(!Comparison{}.deletion_allowed);
} // namespace cmp5_cache_delta
