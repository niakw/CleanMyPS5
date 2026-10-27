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
    s.cache_top_count>CMP5_CACHE_TOP_CAP)return false;
 for(unsigned i=0;i<s.cache_top_count;i++)
  if(s.cache_top[i].partial||!s.cache_top[i].app_label[0]||
     std::strlen(s.cache_top[i].app_label)>=47u)return false;
 return true;
}
inline bool key_equal(const Cmp5CacheTop&a,const Cmp5CacheTop&b) noexcept {
 return a.kind==b.kind&&std::strcmp(a.app_label,b.app_label)==0;
}
inline unsigned matches(const Cmp5HomebrewStats&s,
                        const Cmp5CacheTop&candidate) noexcept {
 unsigned n=0;
 for(unsigned i=0;i<s.cache_top_count;i++)
  if(key_equal(s.cache_top[i],candidate))++n;
 return n;
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
 for(unsigned i=0;i<after.cache_top_count;i++){
  const auto&candidate=after.cache_top[i];
  Entry&entry=result.entries[i];
  std::memcpy(entry.app_label,candidate.app_label,sizeof(entry.app_label));
  entry.kind=candidate.kind;
  entry.new_bytes=candidate.nominal_bytes;
  entry.new_files=candidate.files;
  if(matches(after,candidate)!=1u||matches(before,candidate)!=1u){
   result.incomparable++;continue;
  }
  const Cmp5CacheTop*previous=nullptr;
  for(unsigned j=0;j<before.cache_top_count;j++)
   if(key_equal(before.cache_top[j],candidate)){
    previous=&before.cache_top[j];break;
   }
  if(!previous){result.incomparable++;continue;}
  entry.old_bytes=previous->nominal_bytes;
  entry.old_files=previous->files;
  result.compared++;
  if(previous->nominal_bytes!=candidate.nominal_bytes||
     previous->files!=candidate.files){
   entry.result=Kind::changed;result.changed++;
  }else{
   entry.result=Kind::unchanged_observed;result.unchanged_observed++;
  }
 }
 return result;
}
static_assert(!Comparison{}.idle_verified);
static_assert(!Comparison{}.deletion_allowed);
} // namespace cmp5_cache_delta
