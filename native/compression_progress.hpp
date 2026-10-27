// Narrow stable status protocol of the embedded AMPR engine (GPL-3.0-or-later).
// Exactly one top-level object; rejects unexpected fields and duplicates.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace cmp5_compression {
/* At most one read-only progress poll per eight seconds while its monitor
 * is visible. Never schedule mutations or retry ambiguous POST requests. */
struct ProgressRefresh {
 static constexpr std::uint64_t interval_ns=8000000000ULL;
 std::uint64_t next_ns=0;
 void clear() noexcept {next_ns=0;}
 void defer(std::uint64_t now) noexcept {
  next_ns=now&&now<=UINT64_MAX-interval_ns?now+interval_ns:0;
 }
 bool armed() const noexcept{return next_ns!=0;}
 bool due(std::uint64_t now) const noexcept {
  return next_ns!=0&&now>=next_ns;
 }
};
enum class JobState { idle,running,done,error,cancelled,paused };
enum class JobPhase { idle,starting,waiting,pack,copy,verify,paused,done,error,cancelled,other };
enum class PauseReason { none,user,source_unavailable,destination_unavailable,destination_full,game_active,other };
inline const char* phase_label(JobPhase v) noexcept {
 switch(v){
  case JobPhase::starting:return "DEMARRAGE";
  case JobPhase::waiting:return "ATTENTE DU JEU";
  case JobPhase::pack:return "COMPRESSION LZ4";
  case JobPhase::copy:return "COPIE DES FICHIERS";
  case JobPhase::verify:return "VERIFICATION INTEGRITE";
  case JobPhase::paused:return "EN PAUSE";
  case JobPhase::done:return "TERMINE";
  case JobPhase::error:return "ERREUR";
  case JobPhase::cancelled:return "INTERROMPU";
  case JobPhase::other:return "ETAPE NON IDENTIFIEE";
  default:return "INACTIF";
 }
}
inline const char* reason_label(PauseReason v) noexcept {
 switch(v){
  case PauseReason::user:return "PAUSE DEMANDEE";
  case PauseReason::source_unavailable:return "RECONNECTER STOCKAGE SOURCE";
  case PauseReason::destination_unavailable:return "RECONNECTER STOCKAGE DESTINATION";
  case PauseReason::destination_full:return "ESPACE DESTINATION INSUFFISANT";
  case PauseReason::game_active:return "FERMER LE JEU POUR REPRENDRE";
  case PauseReason::other:return "MOTIF NON IDENTIFIE";
  default:return "AUCUN MOTIF SIGNALE";
 }
}
struct GameProgress {
 JobState state=JobState::idle;
 JobPhase phase=JobPhase::idle;
 PauseReason reason=PauseReason::none;
 unsigned percent=0,queue_len=0;
 std::uint64_t bytes_done=0,bytes_total=0;
 bool launch_checked=false; /* prelaunch check, NOT tested gameplay */
 char title_id[16]{}; /* only a validated 9-character ID can authorize pause */
};
/* Advisory measured transfer rate from successive read-only worker polls.
 * A new title, phase, paused job, backwards counter or invalid clock resets
 * the baseline. This is not a prediction of remaining time or launchability. */
struct ProgressRate {
 char title_id[16]{};
 JobPhase phase=JobPhase::idle;
 std::uint64_t last_ns=0,last_bytes=0;
 unsigned hundredths_mib_per_second=0;
 bool available=false;
 void clear() noexcept {*this=ProgressRate{};}
 void observe(const GameProgress&value,std::uint64_t now) noexcept {
  const bool transfer=value.phase==JobPhase::pack||
    value.phase==JobPhase::copy||value.phase==JobPhase::verify;
  if(!now||value.state!=JobState::running||!transfer||
     !value.title_id[0]||
     !std::memchr(value.title_id,0,sizeof(value.title_id))||
     !value.bytes_total||value.bytes_done>value.bytes_total){
   clear();return;
  }
  const bool same_job=last_ns!=0&&now>last_ns&&
    phase==value.phase&&
    std::memcmp(title_id,value.title_id,sizeof(title_id))==0&&
    value.bytes_done>=last_bytes;
  available=false;
  if(same_job&&now-last_ns>=1000000000ULL){
   const double seconds=static_cast<double>(now-last_ns)/1000000000.0;
   const double rate=static_cast<double>(value.bytes_done-last_bytes)/
       seconds*(100.0/1048576.0);
   if(rate>=0.0&&rate<=1000000.0){
    hundredths_mib_per_second=static_cast<unsigned>(rate+0.5);
    available=true;
   }
  }
  std::memcpy(title_id,value.title_id,sizeof(title_id));
  phase=value.phase;
  last_ns=now;last_bytes=value.bytes_done;
 }
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
  if(!token("\",\"phase\":\""))return false;
  if(token("idle"))value.phase=JobPhase::idle;
  else if(token("starting"))value.phase=JobPhase::starting;
  else if(token("waiting"))value.phase=JobPhase::waiting;
  else if(token("pack"))value.phase=JobPhase::pack;
  else if(token("copy"))value.phase=JobPhase::copy;
  else if(token("verify"))value.phase=JobPhase::verify;
  else if(token("paused"))value.phase=JobPhase::paused;
  else if(token("done"))value.phase=JobPhase::done;
  else if(token("error"))value.phase=JobPhase::error;
  else if(token("cancelled"))value.phase=JobPhase::cancelled;
  else if(token("other"))value.phase=JobPhase::other;
  else return false;
  if(!token("\",\"reason\":\""))return false;
  if(token("none"))value.reason=PauseReason::none;
  else if(token("paused_user"))value.reason=PauseReason::user;
  else if(token("paused_source"))value.reason=PauseReason::source_unavailable;
  else if(token("paused_dest"))value.reason=PauseReason::destination_unavailable;
  else if(token("paused_full"))value.reason=PauseReason::destination_full;
  else if(token("paused_game"))value.reason=PauseReason::game_active;
  else if(token("other"))value.reason=PauseReason::other;
  else return false;
  if(!token("\"}")||p!=end||
     (value.state!=JobState::paused&&value.reason!=PauseReason::none)||
     (value.state==JobState::paused&&value.reason==PauseReason::none)||
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
