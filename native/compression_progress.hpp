// Narrow stable status protocol of the embedded AMPR engine (GPL-3.0-or-later).
// Exactly one top-level object; rejects unexpected fields and duplicates.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace cmp5_compression {
enum class JobState { idle,running,done,error,cancelled,paused };
struct GameProgress {
 JobState state=JobState::idle;
 unsigned percent=0,queue_len=0;
 std::uint64_t bytes_done=0,bytes_total=0;
 bool launch_checked=false; /* prelaunch check, NOT tested gameplay */
 char title_id[16]{}; /* only a validated 9-character ID can authorize pause */
};
class ProgressParser {
 const char*p;const char*end;
 bool token(const char*s) noexcept {
  const auto n=std::strlen(s);
  if(static_cast<std::size_t>(end-p)<n||std::memcmp(p,s,n))return false;
  p+=n;return true;
 }
 bool digit(std::uint64_t*out) noexcept {
  if(p==end||*p<'0'||*p>'9')return false;
  if(*p=='0'){
   ++p;if(p<end&&*p>='0'&&*p<='9')return false;
   *out=0;return true;
  }
  std::uint64_t n=0;
  while(p<end&&*p>='0'&&*p<='9'){
   const unsigned d=static_cast<unsigned>(*p-'0');
   if(n>(UINT64_MAX-d)/10u)return false;
   n=n*10u+d;p++;
  }
  *out=n;return true;
 }
public:
 ProgressParser(const char*str,std::size_t size) noexcept:p(str),end(str+size){}
 bool parse(GameProgress*out) noexcept {
  if(!out||!token("{\"service\":\"CleanMyPS5-AMPR\",\"state\":\""))return false;
  GameProgress value{};
  if(token("idle\""))value.state=JobState::idle;
  else if(token("running\""))value.state=JobState::running;
  else if(token("done\""))value.state=JobState::done;
  else if(token("error\""))value.state=JobState::error;
  else if(token("cancelled\""))value.state=JobState::cancelled;
  else if(token("paused\""))value.state=JobState::paused;
  else return false;
  std::uint64_t pct=0,queue=0;
  if(!token(",\"percent\":")||!digit(&pct)||pct>100u||
     !token(",\"bytes_done\":")||!digit(&value.bytes_done)||
     !token(",\"bytes_total\":")||!digit(&value.bytes_total)||
     !token(",\"queue_len\":")||!digit(&queue)||queue>16u||
     !token(",\"launch_checked\":"))return false;
  if(token("true"))value.launch_checked=true;
  else if(token("false"))value.launch_checked=false;
  else return false;
  if(!token(",\"title_id\":\""))return false;
  unsigned chars=0;
  while(p<end&&*p!='"'){
   if(chars>=9u)return false;
   const char c=*p++;
   if(chars<4u?(c<'A'||c>'Z'):(c<'0'||c>'9'))return false;
   value.title_id[chars++]=c;
  }
  if(!token("\"}")||p!=end||
     (chars!=0u&&chars!=9u)||
     (value.bytes_total&&value.bytes_done>value.bytes_total))return false;
  value.percent=static_cast<unsigned>(pct);
  value.queue_len=static_cast<unsigned>(queue);
  *out=value;return true;
 }
};
inline bool parse_progress(const char*json,std::size_t n,GameProgress*out) noexcept{
 if(!json||!out||n<100||n>512)return false;
 GameProgress sample{};
 if(!ProgressParser(json,n).parse(&sample))return false;
 *out=sample;return true;
}
}
