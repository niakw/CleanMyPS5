// CleanMyPS5 — on-demand, bounded READ-ONLY process visibility.
// A process-name snapshot can show presence; it NEVER proves app ownership,
// inactivity, an absent writer, or any permission to clean a file.
// Copyright (C) 2026 CleanMyPS5 contributors. GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
namespace cmp5_process {
enum class State : unsigned { not_requested, observed, unavailable, malformed, over_limit };
struct Snapshot {
 State state=State::not_requested;
 unsigned processes=0,eboot_names=0,elf_names=0,unnamed=0;
 bool safe_to_clean=false; // no algorithm in this project can set true
};
constexpr std::size_t max_bytes=512u*1024u;
constexpr unsigned max_processes=1024u;
// Layout from FreeBSD kinfo_proc on PS5: ki_structsize at 0, ki_pid
// at 72 and representative thread name at 447. Verified external PS5
// implementations, but MUST be checked against actual FW 13.60 output.
// No Title IDs, PIDs, or process names are retained in Snapshot.
inline Snapshot decode(const void*raw,std::size_t n) noexcept {
 Snapshot result{};result.state=State::malformed;
 if(!raw||n<448u||n>max_bytes)return result;
 const auto*data=static_cast<const unsigned char*>(raw);
 std::size_t cursor=0;
 while(cursor<n){
  if(n-cursor<448u)return result;
  std::int32_t size=0,pid=0;
  std::memcpy(&size,data+cursor,sizeof(size));
  std::memcpy(&pid,data+cursor+72u,sizeof(pid));
  if(size<448||size>8192||static_cast<std::size_t>(size)>n-cursor||
     pid<0)return result;
  if(++result.processes>max_processes){
   result.state=State::over_limit;
   return result;
  }
  const char*name=reinterpret_cast<const char*>(data+cursor+447u);
  std::size_t available=static_cast<std::size_t>(size)-447u;
  if(available>16u)available=16u;
  std::size_t used=0;
  while(used<available&&name[used])++used;
  if(!used||used==available)result.unnamed++;
  else if(used==9u&&std::memcmp(name,"eboot.bin",9u)==0)
   result.eboot_names++;
  else if(used>=4u&&std::memcmp(name+used-4u,".elf",4u)==0)
   result.elf_names++;
  cursor+=static_cast<std::size_t>(size);
 }
 if(cursor!=n)return result;
 result.state=State::observed;
 return result;
}
// Reader(out, &capacity): out==nullptr requests size; otherwise returns
// all records. Never accept partial/ambiguous sysctl buffers as complete.
template<class Reader>
Snapshot sample(Reader reader) noexcept {
 Snapshot unknown{};unknown.state=State::unavailable;
 std::size_t need=0;
 if(!reader(nullptr,&need)||need<448u)return unknown;
 if(need>max_bytes)return Snapshot{State::over_limit};
 const std::size_t room=need+need/4u+4096u;
 if(room>max_bytes)return Snapshot{State::over_limit};
 auto*buf=static_cast<unsigned char*>(std::malloc(room));
 if(!buf)return unknown;
 std::size_t size=room;
 const bool ok=reader(buf,&size);
 Snapshot result{};
 if(!ok)result.state=State::unavailable;
 else if(!size||size>room)result.state=State::malformed;
 else result=decode(buf,size);
 std::free(buf);
 return result;
}
static_assert(!Snapshot{}.safe_to_clean);
} // namespace cmp5_process
