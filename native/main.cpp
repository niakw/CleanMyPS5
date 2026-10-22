// CleanMyPS5 native dashboard (GPL-3.0-or-later).
// Real PS5 title: reads app-visible paths locally, without opening a browser.
// Advanced diagnostics require the separately privileged, read-only helper.
#include "demo_renderer.hpp"
#include "loopback_protocol.hpp"
#include "overview_protocol.hpp"
#include "inventory.h"
#include "duplicates.h"
#include "homebrew.h"
#include "capture_companions.h"
#include "diagnostics.h"
#include "media_preview.h"
#include "video_metadata.h"
#include "preview_decode.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <cerrno>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <atomic>
#include <pthread.h>
#include <cstdlib>
#include <ctime>

extern "C" {
 int sceUserServiceInitialize(const void*);
 int sceUserServiceGetInitialUser(int*);
 int scePadInit(void);
 int scePadOpen(int,int,int,const void*);
 int scePadRead(int,void*,int);
 int sceNetSocket(const char*,int,int,int);
 int sceNetConnect(int,const void*,std::uint32_t);
 int sceNetSend(int,const void*,std::size_t,int);
 int sceNetRecv(int,void*,std::size_t,int);
 int sceNetSetsockopt(int,int,int,const void*,std::uint32_t);
 int sceNetSocketClose(int);
 int open(const char*,int,...);
 int close(int);
 }

namespace {
struct RawPadSample {
 std::uint32_t buttons;
 std::uint8_t left_x,left_y,right_x,right_y,l2,r2;
 std::uint8_t reserved[66];
 std::int32_t connected;
 std::uint64_t timestamp_us;
 std::uint8_t extension[16];
 std::uint8_t connected_count;
 std::uint8_t final_reserved[15];
};
static_assert(sizeof(RawPadSample)==120);
static_assert(offsetof(RawPadSample,connected)==0x4c);

struct NetSockaddrIn {
 std::uint8_t length, family;
 std::uint16_t port;
 std::uint32_t address;
 std::uint16_t virtual_port;
 std::uint8_t zero[6];
};
static_assert(sizeof(NetSockaddrIn)==16);

using Color=ps5::demo::Color;
constexpr std::uint32_t l3=0x2,r3=0x4,options=0x8,up=0x10,down=0x40,left=0x80,right=0x20,
  cross=0x4000,circle=0x2000,square=0x8000,triangle=0x1000,
  l1=0x100,r1=0x200,l2=0x400,r2=0x800;
constexpr int row_count=4;
enum class Screen { menu, analyze, access, apps, cleanup, review, search, games, detail, photo, video_info, official, autoload, companions };
enum class Link { unchecked, ok, offline };
const char* const local_paths[]={
 "/app0/sce_sys/param.json", "/download0", "/data", "/user/av_contents"
};
const char* const local_names[]={
 "APP INSTALLEE", "STOCKAGE APP", "STOCKAGE HOMEBREW", "GALERIE PS5"
};
int controller=-1,selected=0;
Screen screen=Screen::menu;
std::uint32_t previous=0;
Link engine=Link::unchecked;
unsigned available=0,denied=0,absent=0;
enum class LocalAccess { unchecked, unavailable, missing, denied, opened, listable, enumeration_denied };
struct LocalProbe {
 LocalAccess status=LocalAccess::unchecked;
 unsigned entries=0;
 bool truncated=false;
 bool io_error=false;
 bool volume_known=false;
 std::uint64_t available_bytes=0;
 std::uint64_t total_bytes=0;
};
LocalProbe local_probe[4]{};
bool local_checked=false;
std::atomic<bool> scan_cancel_requested{false};
constexpr unsigned max_entries_per_root=256;

int cancellation_hook(void*) noexcept {
 return scan_cancel_requested.load(std::memory_order_acquire)?1:0;
}

LocalAccess open_error(int value) noexcept {
 if(value==ENOENT)return LocalAccess::missing;
 if(value==EACCES||value==EPERM)return LocalAccess::denied;
 return LocalAccess::unavailable;
}
LocalProbe probe_path(const char*path,bool file) noexcept {
 LocalProbe result{};
 if(!path||cancellation_hook(nullptr))return result;
 // One O_NOFOLLOW open anchors both the capacity query and enumeration.
 const int fd=open(path,O_RDONLY|O_NOFOLLOW|
   (file?O_NONBLOCK:O_DIRECTORY));
 if(fd<0){result.status=open_error(errno);return result;}
 if(file){
  struct stat st{};
  if(fstat(fd,&st)!=0||!S_ISREG(st.st_mode)){
   (void)close(fd);
   result.status=LocalAccess::unavailable;
   return result;
  }
 }
 struct statvfs volume{};
 if(fstatvfs(fd,&volume)==0){
  const std::uint64_t block=volume.f_frsize?volume.f_frsize:volume.f_bsize;
  if(block&&static_cast<std::uint64_t>(volume.f_bavail)<=UINT64_MAX/block&&
     static_cast<std::uint64_t>(volume.f_blocks)<=UINT64_MAX/block){
   const auto free=static_cast<std::uint64_t>(volume.f_bavail)*block;
   const auto total=static_cast<std::uint64_t>(volume.f_blocks)*block;
   if(total>0&&free<=total){
    result.volume_known=true;
    result.available_bytes=free;
    result.total_bytes=total;
   }
  }
 }
 if(file){
  (void)close(fd);
  result.status=LocalAccess::opened;
  return result;
 }
 errno=0;
 DIR*dir=fdopendir(fd);
 if(!dir){
  const int error=errno;
  (void)close(fd);
  result.status=open_error(error)==LocalAccess::missing?
    LocalAccess::missing:open_error(error)==LocalAccess::denied?
    LocalAccess::denied:LocalAccess::enumeration_denied;
  return result;
 }
 result.status=LocalAccess::listable;
 for(;;){
  if(cancellation_hook(nullptr))break;
  errno=0;
  struct dirent*entry=readdir(dir);
  if(!entry){
   if(errno!=0)result.io_error=true;
   break;
  }
  if(std::strcmp(entry->d_name,".")==0||
     std::strcmp(entry->d_name,"..")==0)continue;
  if(result.entries==max_entries_per_root){
   result.truncated=true;
   break;
  }
  ++result.entries;
 }
 (void)closedir(dir);
 if(cancellation_hook(nullptr))return LocalProbe{}; // Ignore partial data
 return result;
}
void probe_local() noexcept {
 local_checked=false;
 // Drop previously sampled rights and capacity before every new probe.
 for(auto&sample:local_probe)sample=LocalProbe{};
 for(std::size_t i=0;i<4;i++){
  if(cancellation_hook(nullptr))return;
  local_probe[i]=probe_path(local_paths[i],i==0);
  if(cancellation_hook(nullptr))return;
 }
 local_checked=true;
}

enum class Summary { unchecked, ok, unavailable };
enum class ScanSource { none, native_title, helper };
Summary summary_status=Summary::unchecked;
ScanSource scan_source=ScanSource::none;
unsigned summary_files=0,summary_media=0,summary_cache=0,summary_logs=0;
unsigned summary_large=0,summary_old=0,summary_old_large=0;
std::uint64_t summary_bytes=0,summary_large_bytes=0;
bool summary_partial=false;
Cmp5MediaReview media_review{};
enum class MediaType : unsigned { all, image, video };
enum class MediaSort : unsigned { size, recent, name };
MediaType review_type=MediaType::all;
MediaSort review_sort=MediaSort::size;
bool review_older=false,review_large=false,review_marked_only=false;
char review_game[24]{};
char review_query[21]{}; // Session-only uppercase ASCII. No console OS keyboard.
unsigned keyboard_position=0;
constexpr unsigned keyboard_columns=6;
constexpr unsigned keyboard_rows=6;
constexpr char keyboard_chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
std::uint32_t review_order[CMP5_REVIEW_CAP]{};
unsigned review_visible=0;
unsigned review_selection=0;
unsigned detail_index=0;
Cmp5PreviewImage decoded_photo{};
bool photo_ready=false;
Cmp5VideoMetadata video_info{};
bool video_info_ready=false;
struct CompanionSummary {
 unsigned files=0,sidecars=0,media=0;
 std::uint64_t bytes=0;
};
CompanionSummary companion_summary{};
bool companion_checked=false;
unsigned photo_zoom=100; // 100%, 135%, 170% display-only scaling
bool marked_for_review[CMP5_REVIEW_CAP]{};
unsigned marked_count=0;
std::uint64_t marked_nominal_bytes=0;
unsigned marked_images=0,marked_videos=0;
/* Selection is a review plan, NEVER a permission to delete media. */
void rebuild_marked_totals() noexcept {
 marked_count=marked_images=marked_videos=0;
 marked_nominal_bytes=0;
 for(unsigned i=0;i<media_review.count&&i<CMP5_REVIEW_CAP;i++){
  if(!marked_for_review[i])continue;
  marked_count++;
  if(media_review.entries[i].media_type==1u)marked_images++;
  if(media_review.entries[i].media_type==2u)marked_videos++;
  const auto size=media_review.entries[i].bytes;
  marked_nominal_bytes=(UINT64_MAX-marked_nominal_bytes<size)?
   UINT64_MAX:marked_nominal_bytes+size;
 }
}
Cmp5DuplicateStats duplicate_stats{};
bool duplicates_checked=false;
Cmp5HomebrewStats homebrew_stats{};
bool homebrew_checked=false;
Cmp5AutoloadStats autoload_stats{};
bool autoload_checked=false;
constexpr unsigned game_capacity=256;
struct GameSummary {
 char id[24]{};
 char name[56]{};
 unsigned count=0;
 unsigned marked=0;
 std::uint64_t bytes=0;
};
GameSummary games[game_capacity]{};
unsigned game_count=0,game_selection=0;
bool games_truncated=false;

void rebuild_games() noexcept {
 game_count=0;games_truncated=false;
 for(unsigned i=0;i<media_review.count;i++){
  const auto&item=media_review.entries[i];
  const char*title=item.title_id[0]?item.title_id:"INCONNU";
  unsigned index=0;
  while(index<game_count&&std::strcmp(games[index].id,title)!=0)index++;
  if(index==game_count){
   if(game_count==game_capacity){games_truncated=true;continue;}
   games[index]=GameSummary{};
   (void)std::snprintf(games[index].id,sizeof(games[index].id),"%s",title);
   game_count++;
  }
  auto&game=games[index];
  if(!game.name[0]&&item.game_name[0])
   (void)std::snprintf(game.name,sizeof(game.name),"%s",item.game_name);
  game.count++;
  if(marked_for_review[i])game.marked++;
  game.bytes=(UINT64_MAX-game.bytes<item.bytes)?UINT64_MAX:game.bytes+item.bytes;
 }
 for(unsigned i=1;i<game_count;i++){
  GameSummary current=games[i];
  unsigned j=i;
  while(j&&games[j-1].bytes<current.bytes){
   games[j]=games[j-1];--j;
  }
  games[j]=current;
 }
 if(game_selection>=game_count)game_selection=game_count?game_count-1:0;
}

int compare_media_indices(const void*a,const void*b) noexcept {
 const auto x=*static_cast<const std::uint32_t*>(a);
 const auto y=*static_cast<const std::uint32_t*>(b);
 const auto&one=media_review.entries[x];
 const auto&two=media_review.entries[y];
 int comparison=0;
 if(review_sort==MediaSort::size){
  if(one.bytes!=two.bytes)comparison=one.bytes>two.bytes?-1:1;
 }else if(review_sort==MediaSort::recent){
  if(one.modified_at!=two.modified_at)
   comparison=one.modified_at>two.modified_at?-1:1;
 }else{
  comparison=std::strcmp(one.label,two.label);
 }
 if(comparison)return comparison;
 if(one.bytes!=two.bytes)return one.bytes>two.bytes?-1:1;
 return x<y?-1:x>y?1:0;
}
void rebuild_review_index() noexcept {
 review_visible=0;
 const auto now=std::time(nullptr);
 const bool valid_clock=now>90*86400;
 const auto cutoff=valid_clock?now-90*86400:0;
 for(unsigned i=0;i<media_review.count;i++){
  const auto&entry=media_review.entries[i];
  if(review_marked_only&&!marked_for_review[i])continue;
  if(review_type==MediaType::image&&entry.media_type!=1u)continue;
  if(review_type==MediaType::video&&entry.media_type!=2u)continue;
  if(review_game[0]&&
     ((std::strcmp(review_game,"INCONNU")==0)?
       entry.title_id[0]!=0:std::strcmp(review_game,entry.title_id)!=0))continue;
  if(review_query[0]&&
     !std::strstr(entry.label,review_query)&&
     !std::strstr(entry.title_id,review_query))continue;
  if(review_large&&entry.bytes<50ULL*1024*1024)continue;
  if(review_older&&(!valid_clock||entry.modified_at<=0||entry.modified_at>cutoff))continue;
  review_order[review_visible++]=i;
 }
 std::qsort(review_order,review_visible,sizeof(review_order[0]),compare_media_indices);
 if(review_selection>=review_visible)review_selection=review_visible?review_visible-1:0;
}

/* Square in the per-game overview changes an in-memory review plan only.
 * Include all scanned media for the selected title, independent of current
 * text, type or date filters. No PS5 filesystem mutation can occur here.
 */
void toggle_game_marks(unsigned selected_game) noexcept {
 if(selected_game>=game_count)return;
 auto&game=games[selected_game];
 const bool mark=game.marked<game.count;
 for(unsigned i=0;i<media_review.count&&i<CMP5_REVIEW_CAP;i++){
  const auto&item=media_review.entries[i];
  const char*id=item.title_id[0]?item.title_id:"INCONNU";
  if(std::strcmp(game.id,id)==0)marked_for_review[i]=mark;
 }
 game.marked=mark?game.count:0;
 rebuild_marked_totals();
 if(review_marked_only)rebuild_review_index();
}
unsigned official_selection=0;
bool official_checked[8]{}; // Session-only self-checks; no filesystem changes.

// Native UI uses only fixed paths and a closed loopback listener.
// Bounded receive buffers; the backend never receives a write command.
bool fetch_local(const char*route,char*response,std::size_t capacity,
                 const char**json) noexcept {
 if(!route||!response||capacity<256||!json)return false;
 if(std::strcmp(route,"/api/preflight")&&std::strcmp(route,"/api/overview")&&
    std::strcmp(route,"/api/capabilities"))return false;
 int socket=sceNetSocket("cleanmyps5_ui",2,1,6);
 if(socket<0)return false;
 const int timeout_us=std::strcmp(route,"/api/overview")==0?6000000:350000;
 (void)sceNetSetsockopt(socket,0xffff,0x1105,&timeout_us,sizeof(timeout_us));
 (void)sceNetSetsockopt(socket,0xffff,0x1106,&timeout_us,sizeof(timeout_us));
 const NetSockaddrIn local{16,2,static_cast<std::uint16_t>((8877u<<8)|(8877u>>8)),0x0100007f,0,{0}};
 bool ok=sceNetConnect(socket,&local,sizeof(local))>=0;
 char request[128]{};
 int length=std::snprintf(request,sizeof(request),
   "GET %s HTTP/1.1\r\nHost: 127.0.0.1:8877\r\nConnection: close\r\n\r\n",route);
 if(length<=0||static_cast<std::size_t>(length)>=sizeof(request))ok=false;
 std::size_t sent=0;
 while(ok&&sent<static_cast<std::size_t>(length)){
  const int got=sceNetSend(socket,request+sent,static_cast<std::size_t>(length)-sent,0);
  if(got<=0){ok=false;break;}
  sent+=static_cast<std::size_t>(got);
 }
 std::size_t received=0;
 while(ok&&received<capacity-1){
  const int got=sceNetRecv(socket,response+received,capacity-received-1,0);
  if(got<0){ok=false;break;}
  if(got==0)break;
  received+=static_cast<std::size_t>(got);
 }
 (void)sceNetSocketClose(socket);
 if(!ok||received==0||received>=capacity-1)return false;
 response[received]='\0';
 cmp5_native::HttpBody body{};
 if(!cmp5_native::parse_http_json(response,received,&body))return false;
 *json=body.data;
 return true;
}
bool helper_is_compatible() noexcept {
 char response[1024]{};
 const char*json=nullptr;
 if(!fetch_local("/api/capabilities",response,sizeof(response),&json))return false;
 return cmp5_native::matches_expected_helper(json,std::strlen(json));
}
bool get_preflight() noexcept {
 available=denied=absent=0;
 char response[18432]{};
 const char*json=nullptr;
 if(!fetch_local("/api/preflight",response,sizeof(response),&json))return false;
 cmp5_native::Preflight parsed{};
 if(!cmp5_native::parse_preflight(json,std::strlen(json),&parsed))return false;
 available=parsed.accessible;
 denied=parsed.denied;
 absent=parsed.absent;
 return true;
}
bool accept_overview(const char*json,ScanSource source) noexcept {
 if(!json)return false;
 cmp5_native::Overview parsed{};
 // Reject malformed or duplicated JSON keys instead of matching a fragment
 // that could occur within an unrelated nested object or quoted value.
 if(!cmp5_native::parse_overview(json,std::strlen(json),&parsed))return false;
 // Publish all fields only after one coherent, validated snapshot.
 summary_files=parsed.files;
 summary_media=parsed.media;
 summary_cache=parsed.cache;
 summary_logs=parsed.logs;
 summary_bytes=parsed.bytes;
 summary_large=parsed.large;
 summary_old=parsed.old;
 summary_old_large=parsed.old_large;
 summary_large_bytes=parsed.large_bytes;
 summary_partial=parsed.partial;
 summary_status=Summary::ok;
 scan_source=source;
 return true;
}
void run_overview() noexcept {
 // Any new scan invalidates all review indices and saved per-game totals.
 // In particular, an unavailable/invalid scan MUST NOT expose stale media
 // through the Square catalogue shortcut or detail view. Wipe the private
 // source paths as well as the row counts after every rescan.
 std::memset(&media_review,0,sizeof(media_review));
 media_review.partial=1;
 review_visible=review_selection=detail_index=0;
 game_count=game_selection=0;
 games_truncated=false;
 review_marked_only=false;
 std::memset(marked_for_review,0,sizeof(marked_for_review));
 marked_count=marked_images=marked_videos=0;
 marked_nominal_bytes=0;
 duplicate_stats=Cmp5DuplicateStats{};
 duplicates_checked=false;
 companion_summary=CompanionSummary{};
 companion_checked=false;
 // The same limited scanner is linked inside eboot.bin. It has only
 // the permissions actually granted to the installed title's sandbox.
 // Never present inaccessible roots as empty or deleted.
 summary_status=Summary::unavailable;
 scan_source=ScanSource::none;
 // The title itself discovers media independently of the helper.
 // Its results may be partial because the sandbox cannot read all roots.
 std::size_t length=0;
 char*json=cmp5_overview_with_review_cancel("/",&length,&media_review,cancellation_hook,nullptr);
 bool accepted=false;
 if(json){
  if(!cancellation_hook(nullptr))
   accepted=accept_overview(json,ScanSource::native_title)&&
     media_review.count<=CMP5_REVIEW_CAP&&
     media_review.count<=media_review.media_scanned;
  std::free(json);
 }
 if(!accepted){
  // Scanner failure or invalid JSON may have left an incomplete snapshot.
  // Remove all unpublished private locators, not merely the published count.
  std::memset(&media_review,0,sizeof(media_review));
  media_review.partial=1;
  review_visible=review_selection=detail_index=0;
  game_count=game_selection=0;
  games_truncated=false;
  summary_status=Summary::unavailable;
  scan_source=ScanSource::none;
 }
 if(cancellation_hook(nullptr))return;
 // Optionally prefer helper aggregate counters when it is already running.
 // Media review remains restricted to paths the installed title can see.
 if((summary_status!=Summary::ok||summary_partial)&&helper_is_compatible()){
  char response[2048]{};
  const char*remote=nullptr;
  if(fetch_local("/api/overview",response,sizeof(response),&remote))
   (void)accept_overview(remote,ScanSource::helper);
 }
}
void run_environment_check() noexcept {
 probe_local();
 if(cancellation_hook(nullptr))return;
 engine=Link::offline;
 if(helper_is_compatible()&&!cancellation_hook(nullptr)&&get_preflight())engine=Link::ok;
 if(!cancellation_hook(nullptr))run_overview();
}
void run_duplicate_check() noexcept {
 duplicate_stats=Cmp5DuplicateStats{};
 duplicates_checked=false;
 std::size_t length=0;
 // Content comparison only after an explicit Triangle press.
 // Limits: 128 images, 32 MiB each, 128 MiB IO, ~6 s. No writes.
 char*report=cmp5_duplicates_with_stats_cancel(
   "/",&length,&duplicate_stats,cancellation_hook,nullptr);
 if(report){
  std::free(report); // Never bring private paths into the native UI.
  duplicates_checked=!cancellation_hook(nullptr);
 }
}
void run_photo_preview() noexcept {
 cmp5_release_preview(&decoded_photo);
 photo_ready=false;
 if(detail_index>=media_review.count)return;
 const auto&entry=media_review.entries[detail_index];
 if(entry.media_type!=1u||!entry.preview_path[0])return;
 size_t content_length=0;
 uint8_t*data=cmp5_read_photo_snapshot("/",&entry,&content_length);
 if(!data)return;
 if(!cancellation_hook(nullptr))
  photo_ready=cmp5_decode_preview(data,content_length,&decoded_photo)!=0;
 std::free(data);
 if(cancellation_hook(nullptr)){cmp5_release_preview(&decoded_photo);photo_ready=false;}
}
void run_video_info() noexcept {
 video_info=Cmp5VideoMetadata{};
 video_info_ready=false;
 if(detail_index>=media_review.count)return;
 const auto&entry=media_review.entries[detail_index];
 if(entry.media_type!=2u||!entry.preview_path[0])return;
 video_info_ready=cmp5_probe_video_snapshot("/",&entry,&video_info,
   cancellation_hook,nullptr)!=0;
}
void run_companion_check() noexcept {
 companion_summary=CompanionSummary{};
 companion_checked=false;
 if(detail_index>=media_review.count)return;
 const auto&primary=media_review.entries[detail_index];
 if((primary.media_type!=1u&&primary.media_type!=2u)||
    !primary.preview_path[0]||cancellation_hook(nullptr))return;
 Cmp5MediaCandidate companions[CMP5_GROUP_MAX]{};
 std::uint32_t count=0;
 const bool found=cmp5_plan_capture_group("/", &primary,companions,
   &count,cancellation_hook,nullptr)!=0;
 if(found&&count>=2u&&count<=CMP5_GROUP_MAX&&!cancellation_hook(nullptr)){
  CompanionSummary summary{};
  summary.files=count;
  for(unsigned i=0;i<count;i++){
   if(companions[i].media_type==3u)summary.sidecars++;
   else summary.media++;
   const auto bytes=companions[i].bytes;
   summary.bytes=UINT64_MAX-summary.bytes<bytes?UINT64_MAX:summary.bytes+bytes;
  }
  companion_summary=summary;
  companion_checked=true;
 }
 // Do not publish persistent private paths or candidate file names.
 std::memset(companions,0,sizeof(companions));
}
void run_homebrew_check() noexcept {
 homebrew_stats=Cmp5HomebrewStats{};
 homebrew_checked=false;
 std::size_t length=0;
 // The JSON contains homebrew paths, so never pass it to the title UI.
 char*report=cmp5_homebrew_with_stats("/", &length, &homebrew_stats,
   cancellation_hook, nullptr);
 if(report){
  std::free(report);
  homebrew_checked=!cancellation_hook(nullptr);
 }
}
void run_autoload_check() noexcept {
 autoload_stats=Cmp5AutoloadStats{};
 autoload_checked=false;
 std::size_t length=0;
 // The same read-only scanner used by the optional helper runs under
 // title permissions. Drop its JSON immediately: the UI uses counts only.
 char*report=cmp5_diagnostics_with_stats("/",&length,&autoload_stats);
 if(report){
  std::free(report);
  autoload_checked=!cancellation_hook(nullptr)&&autoload_stats.checked==11u;
 }
}
enum class Task : unsigned { none, environment, access, overview, duplicates, homebrew, autoload, photo, video_info, companions };
std::atomic<unsigned> task_state{0}; // 0=idle, 1=busy, 2=complete, 3=start failed, 4=cancelled
Task task_kind=Task::none;

void* background_task(void*) noexcept {
 switch(task_kind){
  case Task::environment: run_environment_check(); break;
  case Task::access: probe_local(); break;
  case Task::overview: run_overview(); break;
  case Task::duplicates: run_duplicate_check(); break;
  case Task::homebrew: run_homebrew_check(); break;
  case Task::autoload: run_autoload_check(); break;
  case Task::photo: run_photo_preview(); break;
  case Task::video_info: run_video_info(); break;
  case Task::companions: run_companion_check(); break;
  default: break;
 }
 // Reject incomplete results and publish completion AFTER all writes.
 // All read/write UI controls are gated on task_busy() until this release.
 if(cancellation_hook(nullptr)){
  if(task_kind==Task::overview||task_kind==Task::environment){
   std::memset(&media_review,0,sizeof(media_review));
   media_review.partial=1;
   review_visible=review_selection=detail_index=0;
   game_count=game_selection=0;
   summary_status=Summary::unavailable;
   scan_source=ScanSource::none;
  }
  if(task_kind==Task::duplicates){
   duplicate_stats=Cmp5DuplicateStats{};
   duplicates_checked=false;
  }
  if(task_kind==Task::homebrew){
   homebrew_stats=Cmp5HomebrewStats{};
   homebrew_checked=false;
  }
  if(task_kind==Task::autoload){
   autoload_stats=Cmp5AutoloadStats{};
   autoload_checked=false;
  }
  if(task_kind==Task::photo){
   cmp5_release_preview(&decoded_photo);
   photo_ready=false;
  }
  if(task_kind==Task::video_info){
   video_info=Cmp5VideoMetadata{};
   video_info_ready=false;
  }
  if(task_kind==Task::companions){
   companion_summary=CompanionSummary{};
   companion_checked=false;
  }
  if(task_kind==Task::access||task_kind==Task::environment)local_checked=false;
  task_state.store(4,std::memory_order_release);
 }else task_state.store(2,std::memory_order_release);
 return nullptr;
}
bool begin_task(Task kind) noexcept {
 if(task_state.load(std::memory_order_acquire)==1)return false;
 task_kind=kind;
 scan_cancel_requested.store(false,std::memory_order_release);
 task_state.store(1,std::memory_order_release);
 pthread_t worker{};
 const int rc=pthread_create(&worker,nullptr,background_task,nullptr);
 if(rc!=0){
  task_state.store(3,std::memory_order_release);
  return false;
 }
 (void)pthread_detach(worker);
 return true;
}
bool task_busy() noexcept {
 return task_state.load(std::memory_order_acquire)==1;
}
bool task_failed() noexcept {
 return task_state.load(std::memory_order_acquire)==3;
}
bool task_cancelled() noexcept {
 return task_state.load(std::memory_order_acquire)==4;
}
/* A cancellation can race with a worker's final completion check:
 * Circle may navigate away while the worker still reports "busy".
 * Reclaim deferred previews on the first safe UI frame after publication.
 * The acquire load of task_state synchronizes with the worker's release.
 */
void release_inactive_previews() noexcept {
 if(task_busy())return;
 if(screen!=Screen::photo && (decoded_photo.pixels||photo_ready)){
  cmp5_release_preview(&decoded_photo);
  photo_ready=false;
 }
 if(screen!=Screen::video_info && video_info_ready){
  video_info=Cmp5VideoMetadata{};
  video_info_ready=false;
 }
 if(screen!=Screen::companions && companion_checked){
  companion_summary=CompanionSummary{};
  companion_checked=false;
 }
}
void poll_pad() noexcept {
 if(controller<0)return;
 RawPadSample samples[32]{};
 const int count=scePadRead(controller,samples,32);
 if(count<=0)return;
 for(int i=0;i<count;i++){
  const std::uint32_t buttons=samples[i].connected!=0?samples[i].buttons:0;
  const std::uint32_t pressed=buttons&~previous;
  previous=buttons;
  if(pressed&circle){
   if(task_busy())scan_cancel_requested.store(true,std::memory_order_release);
   else if(screen==Screen::photo){
    // Do not retain decoded RGBA buffers after closing the preview;
    // avoid touching the buffer while its worker is still running.
    cmp5_release_preview(&decoded_photo);
    photo_ready=false;
   }else if(screen==Screen::video_info){
    video_info=Cmp5VideoMetadata{};
    video_info_ready=false;
   }
   screen=(screen==Screen::photo||screen==Screen::video_info||
            screen==Screen::companions)?Screen::detail:
     (screen==Screen::detail||screen==Screen::search||
           screen==Screen::games)?Screen::review:
     screen==Screen::apps?Screen::access:
     screen==Screen::review?Screen::cleanup:
     screen==Screen::autoload?Screen::official:Screen::menu;
   continue;
  }
  if(task_busy())continue;
  if(screen==Screen::search){
   if(pressed&left)keyboard_position=(keyboard_position+35u)%36u;
   if(pressed&right)keyboard_position=(keyboard_position+1u)%36u;
   if(pressed&up)keyboard_position=(keyboard_position+30u)%36u;
   if(pressed&down)keyboard_position=(keyboard_position+6u)%36u;
   if(pressed&cross){
    const std::size_t length=std::strlen(review_query);
    if(length<sizeof(review_query)-1u){
     review_query[length]=keyboard_chars[keyboard_position];
     review_query[length+1u]=0;
     review_selection=0;
     rebuild_review_index();
    }
   }
   if(pressed&triangle){
    const std::size_t length=std::strlen(review_query);
    if(length){review_query[length-1u]=0;review_selection=0;rebuild_review_index();}
   }
   if(pressed&square){
    if(review_query[0]){
     review_query[0]=0;review_selection=0;rebuild_review_index();
    }
   }
   continue;
  }
  if(screen==Screen::games){
   if(game_count){
    if(pressed&up)game_selection=(game_selection+game_count-1)%game_count;
    if(pressed&down)game_selection=(game_selection+1)%game_count;
    if(pressed&left)game_selection=game_selection>4?game_selection-4:0;
    if(pressed&right)game_selection=(game_selection+4<game_count)?
      game_selection+4:game_count-1;
    if(pressed&square){toggle_game_marks(game_selection);continue;}
    if(pressed&cross){
     const char*id=games[game_selection].id;
     (void)std::snprintf(review_game,sizeof(review_game),"%s",id);
     review_selection=0;rebuild_review_index();
     screen=Screen::review;continue;
    }
   }
   if(pressed&triangle){
    review_game[0]=0;review_selection=0;rebuild_review_index();
    screen=Screen::review;
   }
   continue;
  }
  if(screen==Screen::video_info||screen==Screen::companions)continue;
  if(screen==Screen::photo){
   if(pressed&l2)photo_zoom=photo_zoom==170?135:100;
   if(pressed&r2)photo_zoom=photo_zoom==100?135:170;
   if((pressed&l1||pressed&r1)&&review_visible){
    const unsigned direction=(pressed&r1)?1u:review_visible-1u;
    for(unsigned offset=1;offset<=review_visible;offset++){
     const unsigned candidate=(review_selection+offset*direction)%review_visible;
     const auto&item=media_review.entries[review_order[candidate]];
     if(item.media_type==1u&&item.preview_path[0]){
      review_selection=candidate;
      detail_index=review_order[candidate];
      photo_zoom=100;
      (void)begin_task(Task::photo);
      break;
     }
    }
   }
   continue;
  }
  if(screen==Screen::detail){
   if((pressed&square)&&detail_index<media_review.count){
    screen=Screen::companions;
    (void)begin_task(Task::companions);
    continue;
   }
   if((pressed&cross)&&detail_index<media_review.count){
    const auto&item=media_review.entries[detail_index];
    if(item.preview_path[0]&&item.media_type==1u){
     screen=Screen::photo;
     photo_zoom=100;
     (void)begin_task(Task::photo);
    }else if(item.preview_path[0]&&item.media_type==2u){
     screen=Screen::video_info;
     (void)begin_task(Task::video_info);
    }
    continue;
   }
   if((pressed&triangle)&&detail_index<media_review.count){
    bool&item=marked_for_review[detail_index];
    item=!item;
    rebuild_marked_totals();
    if(review_marked_only)rebuild_review_index();
   }
   continue;
  }
  if(screen==Screen::review){
   if(pressed&r3){
    review_marked_only=!review_marked_only;
    review_selection=0;rebuild_review_index();
    continue;
   }
   if(pressed&l3){rebuild_games();screen=Screen::games;continue;}
   if(pressed&options){screen=Screen::search;continue;}
   if(pressed&square&&review_visible){
    detail_index=review_order[review_selection];
    screen=Screen::detail;
    continue;
   }
   if(pressed&l1||pressed&r1){
    const unsigned step=(pressed&r1)?1u:2u;
    review_type=static_cast<MediaType>((static_cast<unsigned>(review_type)+step)%3u);
    review_selection=0;
    rebuild_review_index();
   }
   if(pressed&triangle){
    review_sort=static_cast<MediaSort>((static_cast<unsigned>(review_sort)+1u)%3u);
    review_selection=0;
    rebuild_review_index();
   }
   if(pressed&l2){review_older=!review_older;review_selection=0;rebuild_review_index();}
   if(pressed&r2){review_large=!review_large;review_selection=0;rebuild_review_index();}
   if(pressed&cross){
    if(review_visible){
     const auto&entry=media_review.entries[review_order[review_selection]];
     const char*id=entry.title_id[0]?entry.title_id:"INCONNU";
     if(std::strcmp(review_game,id)==0)review_game[0]=0;
     else (void)std::snprintf(review_game,sizeof(review_game),"%s",id);
     review_selection=0;rebuild_review_index();
    }else if(review_game[0]){
     review_game[0]=0;review_selection=0;rebuild_review_index();
    }
   }
   if(review_visible){
    if(pressed&up)review_selection=(review_selection+review_visible-1)%review_visible;
    if(pressed&down)review_selection=(review_selection+1)%review_visible;
    if(pressed&left)review_selection=review_selection>4?review_selection-4:0;
    if(pressed&right)review_selection=(review_selection+4<review_visible)?
      review_selection+4:review_visible-1;
   }
   continue;
  }
  if(screen==Screen::autoload){
   if(pressed&cross)(void)begin_task(Task::autoload);
   continue;
  }
  if(screen==Screen::official){
   if(pressed&square){
    screen=Screen::autoload;
    (void)begin_task(Task::autoload);
    continue;
   }
   if(pressed&up)official_selection=(official_selection+7u)%8u;
   if(pressed&down)official_selection=(official_selection+1u)%8u;
   if(pressed&cross)official_checked[official_selection]=!official_checked[official_selection];
   continue;
  }
  if(screen==Screen::access&&(pressed&square)){
   screen=Screen::apps;
   (void)begin_task(Task::homebrew);
   continue;
  }
  if(screen==Screen::cleanup&&(pressed&square)&&!task_cancelled()){
   if(media_review.count){
    review_selection=0;rebuild_review_index();screen=Screen::review;
   }
   continue;
  }
  if(screen==Screen::cleanup&&(pressed&triangle)&&!task_cancelled()){
   (void)begin_task(Task::duplicates);
   continue;
  }
  if(screen==Screen::menu){
   if(pressed&up)selected=(selected+row_count-1)%row_count;
   if(pressed&down)selected=(selected+1)%row_count;
   if(pressed&cross){
    screen=selected==0?Screen::analyze:selected==1?Screen::access:
      selected==2?Screen::cleanup:Screen::official;
    if(screen==Screen::analyze)(void)begin_task(Task::environment);
    if(screen==Screen::cleanup)(void)begin_task(Task::overview);
    if(screen==Screen::access)(void)begin_task(Task::access);
   }
  }else if(pressed&cross){
   if(screen==Screen::analyze||screen==Screen::cleanup)(void)begin_task(Task::overview);
   if(screen==Screen::access)(void)begin_task(Task::access);
   if(screen==Screen::apps)(void)begin_task(Task::homebrew);
  }
 }
}
void header(ps5::demo::Canvas&canvas,const char*subtitle) noexcept {
 canvas.clear(Color::background);
 canvas.rectangle(0,0,1920,12,Color::cyan);
 canvas.rectangle(72,90,1776,135,Color::panel);
 canvas.text(112,130,"CLEANMYPS5",9,Color::white);
 canvas.text(115,255,subtitle,5,Color::cyan);
}
void panel(ps5::demo::Canvas&canvas,unsigned y,const char*name,const char*detail,
           Color tone=Color::white) noexcept {
 canvas.rectangle(95,y,1730,116,Color::panel);
 canvas.text(135,y+20,name,5,tone);
 canvas.text(930,y+26,detail,3,Color::white);
}
void footer(ps5::demo::Canvas&canvas,const char*message) noexcept {
 canvas.rectangle(95,950,1730,96,Color::panel);
 canvas.text(125,977,message,4,Color::white);
}
void show_menu(ps5::demo::Canvas&canvas) noexcept {
 static constexpr const char*items[row_count]={
  "ANALYSE", "ACCES AU STOCKAGE", "NETTOYAGE", "RETOUR OFFICIEL"
 };
 static constexpr const char*detail[row_count]={
  "ETAT DU MOTEUR", "DROITS REELS", "PROTECTION ACTIVE", "CONTROLES LOCAUX"
 };
 for(int i=0;i<row_count;i++){
  const unsigned top=350+static_cast<unsigned>(i)*142;
  canvas.rectangle(95,top,1730,116,i==selected?Color::cyan:Color::panel);
  canvas.rectangle(104,top+9,1712,98,Color::background);
  canvas.text(145,top+30,items[i],5,i==selected?Color::cyan:Color::white);
  canvas.text(1000,top+37,detail[i],3,Color::white);
 }
 footer(canvas,task_busy()?
   "ANALYSE EN COURS    CERCLE POUR ANNULER":
   task_cancelled()?"ANALYSE ANNULEE   X POUR RECOMMENCER":
   "X OUVRIR    HAUT / BAS NAVIGUER");
}
void show_analyze(ps5::demo::Canvas&canvas) noexcept {
 if(task_busy()){
  panel(canvas,366,"ANALYSE EN COURS","LE MENU RESTE UTILISABLE",Color::cyan);
  panel(canvas,515,"DOSSIERS ET PERMISSIONS","CONTROLES EN LECTURE SEULE");
  panel(canvas,664,"MOTEUR DE MAINTENANCE","REPONSE EN ATTENTE");
  footer(canvas,"CERCLE RETOUR  AUCUNE SUPPRESSION");
  return;
 }
 if(task_failed()||task_cancelled()){
  panel(canvas,366,task_cancelled()?"ANALYSE ANNULEE":"CONTROLE NON LANCE",
    task_cancelled()?"RESULTAT PARTIEL REJETE":"ECHEC DE CREATION DU WORKER",
    Color::yellow);
  footer(canvas,"X RETENTER    CERCLE RETOUR");
  return;
 }
 if(engine==Link::ok){
  char result[112]{};
  (void)std::snprintf(result,sizeof(result),"ACCESSIBLES %u  REFUSES %u  ABSENTS %u",
                       available,denied,absent);
  panel(canvas,366,"DIAGNOSTIC DU MOTEUR",result,Color::cyan);
 }else if(summary_status==Summary::ok){
  panel(canvas,366,"SCAN NATIF DU TITRE",
    summary_partial?"COUVERTURE LIMITEE AUX DROITS":"ANALYSE LOCALE DISPONIBLE",
    summary_partial?Color::yellow:Color::cyan);
 }else{
  panel(canvas,366,"ANALYSE NON DISPONIBLE","DROITS OU MOTEUR LIMITES",Color::yellow);
 }
 char local_result[96]{};
 unsigned listable=0,sampled=0;
 bool incomplete=false;
 for(const auto&probe:local_probe){
  if(probe.status==LocalAccess::listable){
   ++listable;
   sampled+=probe.entries;
   incomplete|=probe.truncated||probe.io_error;
  }
 }
 (void)std::snprintf(local_result,sizeof(local_result),
  local_checked?"%u DOSSIERS  %u ENTREES VUES%s":"NON EXECUTEE",
  listable,sampled,incomplete?" (PARTIEL)":"");
 panel(canvas,515,"ANALYSE LOCALE",local_result);
 char detail[112]{};
 if(summary_status==Summary::ok){
  (void)std::snprintf(detail,sizeof(detail),"%u FICHIERS  %llu MIO NOMINAUX%s",
    summary_files,static_cast<unsigned long long>(summary_bytes/1048576ULL),
    summary_partial?"  PARTIEL":"");
  panel(canvas,664,scan_source==ScanSource::native_title?
    "BILAN NATIF DU TITRE":"BILAN HELPER (AGREGATS)",detail,Color::cyan);
  (void)std::snprintf(detail,sizeof(detail),"CAPTURES %u  CACHES %u  LOGS %u",
    summary_media,summary_cache,summary_logs);
  panel(canvas,813,"CATEGORIES VUES",detail);
 }else{
  panel(canvas,664,"BILAN DU STOCKAGE",
    summary_status==Summary::unchecked?"X POUR LANCER LE SCAN":"COUVERTURE INDISPONIBLE",
    Color::yellow);
  panel(canvas,813,"PROTECTION","AUCUNE SUPPRESSION EFFECTUEE");
 }
 footer(canvas,"X ANALYSE APPROFONDIE    CERCLE RETOUR");
}
void show_access(ps5::demo::Canvas&canvas) noexcept {
 if(task_busy()){
  panel(canvas,366,"LECTURE DES DROITS","VERIFICATION EN COURS",Color::cyan);
  footer(canvas,"CERCLE RETOUR  AUCUNE MODIFICATION");
  return;
 }
 if(task_failed()||task_cancelled()){
  panel(canvas,366,task_cancelled()?"LECTURE ANNULEE":"CONTROLE NON LANCE",
   "AUCUNE CONCLUSION SUR LES ACCES",Color::yellow);
  footer(canvas,"X RETENTER    CERCLE RETOUR");
  return;
 }
 for(std::size_t i=0;i<4;i++){
  const auto&probe=local_probe[i];
  const auto state=probe.status;
  char detail[100]{};
  if(state==LocalAccess::listable){
   if(probe.volume_known)
    (void)std::snprintf(detail,sizeof(detail),"%u VUES  %llu/%llu GIO LIBRES/TOTAL%s",
      probe.entries,
      static_cast<unsigned long long>(probe.available_bytes/1073741824ULL),
      static_cast<unsigned long long>(probe.total_bytes/1073741824ULL),
      probe.truncated?" (LIMITE)":probe.io_error?" (ERREUR)":"");
   else
    (void)std::snprintf(detail,sizeof(detail),"%u ENTREES VUES%s",
      probe.entries,probe.truncated?" (LIMITE)":probe.io_error?" (ERREUR)":"");
  }else{
   const char*reason=state==LocalAccess::unchecked?"NON TESTE":
     state==LocalAccess::opened?"FICHIER OUVERT":
     state==LocalAccess::missing?"ABSENT":
     state==LocalAccess::denied?"PERMISSION REFUSEE":
     state==LocalAccess::enumeration_denied?"LISTE BLOQUEE":
     "ERREUR D ACCES";
   (void)std::snprintf(detail,sizeof(detail),"%s",reason);
  }
  const unsigned y=353+static_cast<unsigned>(i)*144;
   panel(canvas,y,local_names[i],
     detail,state==LocalAccess::listable||state==LocalAccess::opened?Color::cyan:Color::yellow);
   if(state==LocalAccess::listable&&probe.volume_known){
    /* Each mount is independent; do not sum capacity across paths. */
    constexpr unsigned bar_width=650;
    const double fraction=static_cast<double>(probe.available_bytes)/
      static_cast<double>(probe.total_bytes);
    const unsigned free_width=static_cast<unsigned>(fraction*bar_width);
    canvas.rectangle(930,y+87,bar_width,12,Color::background);
    if(free_width)canvas.rectangle(930,y+87,free_width,12,Color::cyan);
   }
 }
 footer(canvas,"X ACTUALISER  CARRE APPS ET PLUGINS  CERCLE RETOUR");
}
void show_apps(ps5::demo::Canvas&canvas) noexcept {
 if(task_busy()){
  panel(canvas,355,"APPS ET PLUGINS","INVENTAIRE EN COURS",Color::cyan);
  panel(canvas,503,"LECTURE PROTEGEE","ANALYSE LIMITEE A 3 SECONDES");
  footer(canvas,"CERCLE ANNULER  AUCUNE DESINSTALLATION");
  return;
 }
 if(task_cancelled()||task_failed()||!homebrew_checked){
  panel(canvas,355,"INVENTAIRE INDISPONIBLE",
    task_cancelled()?"ANNULE":"DROITS OU MOTEUR LIMITES",Color::yellow);
  footer(canvas,"X REESSAYER   CERCLE RETOUR");
  return;
 }
 const unsigned counts[4]={
  homebrew_stats.homebrew,homebrew_stats.plugins,
  homebrew_stats.app_data,homebrew_stats.installed
 };
 static constexpr const char*labels[4]={
  "HOMEBREWS","PLUGINS","DONNEES APPS","INSTALLATIONS"
 };
 for(unsigned i=0;i<4;i++){
  char detail[95]{};
  (void)std::snprintf(detail,sizeof(detail),"%u ELEMENTS PROTEGES",counts[i]);
  panel(canvas,355+i*144,labels[i],detail,Color::cyan);
 }
 char footer_message[150]{};
 (void)std::snprintf(footer_message,sizeof(footer_message),
  "X ACTUALISER  %s  CERCLE RETOUR  AUCUN EFFACEMENT",
  homebrew_stats.partial?"SCAN PARTIEL":"PERIMETRE CONNU");
 footer(canvas,footer_message);
}
void show_cleanup(ps5::demo::Canvas&canvas) noexcept {
 if(task_busy()){
  panel(canvas,355,"BILAN DES CAPTURES","ANALYSE EN COURS",Color::cyan);
  panel(canvas,503,"LE SYSTEME RESTE RESPONSIF","CERCLE POUR RETOURNER");
  panel(canvas,799,"PROTECTION","LECTURE SEULE  AUCUNE SUPPRESSION",Color::yellow);
  footer(canvas,"ANALYSE EN COURS    CERCLE RETOUR");
  return;
 }
 if(task_failed()||task_cancelled()){
  panel(canvas,355,task_cancelled()?"ANALYSE ANNULEE":"ANALYSE NON LANCEE",
   "AUCUN RESULTAT UTILISABLE",Color::yellow);
  footer(canvas,"X RETENTER    CERCLE RETOUR");
  return;
 }
 if(summary_status==Summary::ok){
  char result[120]{};
  (void)std::snprintf(result,sizeof(result),"%u A REVOIR (PLUS DE 50 MIO)",summary_large);
  panel(canvas,355,"CAPTURES VOLUMINEUSES",result,Color::cyan);
  (void)std::snprintf(result,sizeof(result),"%u ANCIENNES (MTIME 90 J)",summary_old);
  panel(canvas,503,"CAPTURES ANCIENNES",result,Color::cyan);
  (void)std::snprintf(result,sizeof(result),"%llu MIO NOMINAUX  %u ANCIENNES ET GROSSES",
    static_cast<unsigned long long>(summary_large_bytes/1048576ULL),summary_old_large);
  panel(canvas,651,scan_source==ScanSource::helper?
    "ESPACE A EXAMINER (HELPER)":"ESPACE A EXAMINER",result,Color::yellow);
 }else{
  panel(canvas,355,"CAPTURES VOLUMINEUSES","BILAN NON DISPONIBLE",Color::yellow);
  panel(canvas,503,"CAPTURES ANCIENNES","BILAN NON DISPONIBLE",Color::yellow);
  panel(canvas,651,"MOTEUR","DROITS INSUFFISANTS OU SCAN ECHOUE",Color::yellow);
 }
 char selected_msg[130]{};
 if(duplicates_checked){
  (void)std::snprintf(selected_msg,sizeof(selected_msg),
    "%u MARQUES  %llu MIO NOMINAUX  %u GROUPES%s",marked_count,
     static_cast<unsigned long long>(marked_nominal_bytes/1048576ULL),
     duplicate_stats.exact_groups,duplicate_stats.partial?"  PARTIEL":"");
 }else{
  (void)std::snprintf(selected_msg,sizeof(selected_msg),
    "%u MARQUES  %llu MIO NOMINAUX  TRIANGLE: DOUBLONS",marked_count,
     static_cast<unsigned long long>(marked_nominal_bytes/1048576ULL));
 }
 panel(canvas,799,"REVUE ET DOUBLONS",selected_msg,Color::yellow);
 canvas.rectangle(95,950,1730,96,Color::panel);
 char native_catalog[110]{};
 (void)std::snprintf(native_catalog,sizeof(native_catalog),
   "CARRE CATALOGUE (%u NATIFS)   TRIANGLE DOUBLONS   X SCAN",media_review.count);
 canvas.text(125,963,native_catalog,3,Color::white);
 canvas.text(125,1005,"CERCLE RETOUR   LECTURE SEULE   AUCUNE SUPPRESSION",3,Color::cyan);
}
void show_review(ps5::demo::Canvas&canvas) noexcept {
 if(!media_review.count){
  panel(canvas,367,"CAPTURES NON VISIBLES",scan_source==ScanSource::helper?
    "HELPER: TOTAUX SEULS, AUCUN DETAIL NATIF":"ACCES LIMITE OU AUCUNE CAPTURE",
    Color::yellow);
  footer(canvas,"CERCLE RETOUR  AUCUNE SUPPRESSION");
  return;
 }
 char title[150]{};
 const char*type=review_type==MediaType::image?"IMAGES":
   review_type==MediaType::video?"VIDEOS":"TOUS";
 const char*sort=review_sort==MediaSort::recent?"DATE":
   review_sort==MediaSort::name?"NOM":"TAILLE";
 (void)std::snprintf(title,sizeof(title),"%u / %u MEDIAS  %s  TRI %s%s%s",
  review_visible,media_review.media_scanned,type,sort,
  review_marked_only?"  MARQUES":"",
  media_review.partial?"  PARTIEL":"");
 canvas.text(112,283,title,3,Color::yellow);
 if(review_game[0])canvas.text(1110,283,review_game,3,Color::cyan);
 if(!review_visible){
  panel(canvas,501,"AUCUN RESULTAT","MODIFIER LES FILTRES",Color::yellow);
 }
 const unsigned page=review_selection/4u;
 for(unsigned slot=0;slot<4;slot++){
  const unsigned index=page*4u+slot;
  if(index>=review_visible)break;
  const auto&item=media_review.entries[review_order[index]];
  const unsigned top=358+slot*142u;
  const bool selected_item=index==review_selection;
  canvas.rectangle(95,top,1730,116,selected_item?Color::cyan:Color::panel);
  canvas.rectangle(103,top+8,1714,100,Color::background);
  canvas.text(135,top+17,item.title_id[0]?item.title_id:"JEU INCONNU",4,
    selected_item?Color::cyan:Color::white);
  char label[31]{};
  (void)std::snprintf(label,sizeof(label),"%.30s",item.label);
  canvas.text(620,top+31,label,3,Color::white);
  char stats[78]{};
  (void)std::snprintf(stats,sizeof(stats),"%s%s  %llu MIO",
     marked_for_review[review_order[index]]?"MARQUE ":"",
     item.media_type==2u?"VIDEO":"IMAGE",
    static_cast<unsigned long long>(item.bytes/1048576ULL));
  canvas.text(1320,top+36,stats,3,Color::cyan);
 }
 canvas.rectangle(95,950,1730,96,Color::panel);
 canvas.text(125,962,"L1 R1 TYPE  TRIANGLE TRI  L2 AGE  R2 50 MIO  R3 MARQUES",3,Color::white);
 canvas.text(125,1005,"OPTIONS RECHERCHE  L3 JEUX  X FILTRE JEU  CARRE DETAILS  CERCLE",3,Color::cyan);
}
void show_games(ps5::demo::Canvas&canvas) noexcept {
 char heading[110]{};
 (void)std::snprintf(heading,sizeof(heading),"%u JEUX %s%s",
  game_count,games_truncated?"(LISTE PARTIELLE)":"INVENTORIES",
  media_review.partial?"  SCAN PARTIEL":"");
 canvas.text(112,288,heading,4,Color::yellow);
 if(game_count==0){
  panel(canvas,506,"AUCUN JEU","GALERIE OU DROITS LIMITES",Color::yellow);
 }
 const unsigned page=game_selection/4u;
 for(unsigned slot=0;slot<4u;slot++){
  const unsigned at=page*4u+slot;
  if(at>=game_count)break;
  const auto&game=games[at];
  const unsigned y=351+slot*142u;
  const bool focus=at==game_selection;
  canvas.rectangle(95,y,1730,116,focus?Color::cyan:Color::panel);
  canvas.rectangle(103,y+8,1714,100,Color::background);
  char display[43]{};
  (void)std::snprintf(display,sizeof(display),"%.29s",
    game.name[0]?game.name:game.id);
  canvas.text(145,y+30,display,4,focus?Color::cyan:Color::white);
  char info[100]{};
  (void)std::snprintf(info,sizeof(info),"%u / %u MARQUES  %llu MIO NOMINAUX",
    game.marked,game.count,static_cast<unsigned long long>(game.bytes/1048576ULL));
  canvas.text(1030,y+37,info,3,Color::white);
 }
 canvas.rectangle(95,950,1730,96,Color::panel);
 canvas.text(125,959,"X FILTRER  CARRE MARQUER / DEMARQUER CAPTURES SCANNEES",3,Color::white);
 canvas.text(125,1004,"TRIANGLE TOUS  CERCLE CATALOGUE  REVUE SEULE - AUCUN EFFACEMENT",3,Color::cyan);
}
void show_search(ps5::demo::Canvas&canvas) noexcept {
 char query[95]{};
 (void)std::snprintf(query,sizeof(query),"%s%s",
   review_query[0]?review_query:"RECHERCHER NOM OU JEU",review_query[0]?" _":"");
 panel(canvas,312,"RECHERCHE LOCALE",query,Color::cyan);
 char counter[95]{};
 (void)std::snprintf(counter,sizeof(counter),"%u RESULTATS DANS LE CATALOGUE",
   review_visible);
 canvas.text(125,445,counter,4,Color::yellow);
 for(unsigned i=0;i<36u;i++){
  const unsigned row=i/keyboard_columns,col=i%keyboard_columns;
  const unsigned x=170+col*260u,y=515+row*66u;
  const bool focused=i==keyboard_position;
  canvas.rectangle(x,y,205,54,focused?Color::cyan:Color::panel);
  char glyph[2]{keyboard_chars[i],0};
  canvas.text(x+78,y+7,glyph,5,focused?Color::background:Color::white);
 }
 footer(canvas,"X AJOUTER  TRIANGLE EFFACER  CARRE VIDER  CERCLE RETOUR");
}
void show_detail(ps5::demo::Canvas&canvas) noexcept {
 if(detail_index>=media_review.count){
  panel(canvas,355,"DETAIL INDISPONIBLE","REVENIR AU CATALOGUE",Color::yellow);
  footer(canvas,"CERCLE RETOUR");
  return;
 }
 const auto&item=media_review.entries[detail_index];
 panel(canvas,355,"NOM AFFICHE (ABREGE)",item.label);
 canvas.rectangle(95,503,1730,116,Color::panel);
 canvas.text(135,532,"JEU",5,Color::cyan);
 char game_title[55]{};
 (void)std::snprintf(game_title,sizeof(game_title),"%.46s",
   item.game_name[0]?item.game_name:"NOM INCONNU");
 canvas.text(790,516,game_title,3,Color::white);
 canvas.text(790,558,item.title_id[0]?item.title_id:"ID INCONNU",3,Color::cyan);
 char details[120]{};
 const std::time_t now=std::time(nullptr);
 const bool dated=now>0&&item.modified_at>0&&item.modified_at<=now;
 if(dated){
  (void)std::snprintf(details,sizeof(details),"%s  %llu MIO  MODIFIE IL Y A %llu J",
    item.media_type==2u?"VIDEO":"IMAGE",
    static_cast<unsigned long long>(item.bytes/1048576ULL),
    static_cast<unsigned long long>((now-item.modified_at)/86400));
 }else{
  (void)std::snprintf(details,sizeof(details),"%s  %llu MIO  DATE INCONNUE",
    item.media_type==2u?"VIDEO":"IMAGE",
    static_cast<unsigned long long>(item.bytes/1048576ULL));
 }
 panel(canvas,651,"METADONNEES",details);
 panel(canvas,799,"SELECTION",
   marked_for_review[detail_index]?"MARQUE A REVOIR":"NON MARQUE",
   marked_for_review[detail_index]?Color::cyan:Color::yellow);
 footer(canvas,item.media_type==2u?
  "X INFOS MP4/WEBM  CARRE FICHIERS LIES  TRIANGLE MARQUER  CERCLE":
  "X APERCU PHOTO  CARRE FICHIERS LIES  TRIANGLE MARQUER  CERCLE");
}
void show_companions(ps5::demo::Canvas&canvas) noexcept {
 if(task_busy()){
  panel(canvas,360,"FICHIERS ASSOCIES","VERIFICATION EN LECTURE SEULE",Color::cyan);
  footer(canvas,"CERCLE ANNULER  AUCUNE MODIFICATION");
  return;
 }
 if(task_cancelled()||task_failed()||!companion_checked){
  panel(canvas,360,"GROUPE NON VERIFIABLE",
    task_cancelled()?"ANALYSE ANNULEE":"FICHIERS ABSENTS, INACCESSIBLES OU AMBIGUS",
    Color::yellow);
  panel(canvas,510,"SECURITE","NE PAS SUPPRIMER CETTE CAPTURE",Color::yellow);
  footer(canvas,"CERCLE DETAIL  AUCUN NETTOYAGE AUTORISE");
  return;
 }
 char count[105]{};
 (void)std::snprintf(count,sizeof(count),"%u FICHIERS  %u MEDIAS",
   companion_summary.files,companion_summary.media);
 panel(canvas,360,"GROUPE OBSERVE",count,Color::cyan);
 (void)std::snprintf(count,sizeof(count),"%u FICHIER(S) DAT",companion_summary.sidecars);
 panel(canvas,508,"METADONNEES ASSOCIEES",count,Color::cyan);
 (void)std::snprintf(count,sizeof(count),"%llu MIO NOMINAUX",
   static_cast<unsigned long long>(companion_summary.bytes/1048576ULL));
 panel(canvas,656,"TAILLE DU GROUPE",count);
 panel(canvas,804,"LIMITATION","INDEX GALERIE ET MINIATURES NON PROUVES",Color::yellow);
 canvas.rectangle(95,950,1730,96,Color::panel);
 canvas.text(125,968,"CERCLE DETAIL  AUCUNE SUPPRESSION NI SAUVEGARDE",3,Color::white);
 canvas.text(125,1006,"ASSOCIATIONS VISIBLES SEULEMENT - LECTURE SEULE",3,Color::cyan);
}
void show_photo(ps5::demo::Canvas&canvas) noexcept {
 if(task_busy()){
  panel(canvas,501,"CHARGEMENT PHOTO","DECODAGE LOCAL EN COURS",Color::cyan);
  footer(canvas,"CERCLE ANNULER  AUCUNE ECRITURE");
  return;
 }
 if(task_cancelled()||task_failed()||!photo_ready||
    !decoded_photo.pixels||!decoded_photo.width||!decoded_photo.height){
  panel(canvas,501,"APERCU INDISPONIBLE",
    task_cancelled()?"LECTURE ANNULEE":"FORMAT OU ACCES NON COMPATIBLE",Color::yellow);
  footer(canvas,"CERCLE RETOUR  AUCUNE ECRITURE");
  return;
 }
 canvas.rectangle(220,315,1480,600,Color::panel);
 const unsigned src_w=decoded_photo.width,src_h=decoded_photo.height;
 const unsigned draw_w=(src_w*photo_zoom)/100u;
 const unsigned draw_h=(src_h*photo_zoom)/100u;
 const unsigned dest_x=960u-draw_w/2u;
 const unsigned dest_y=615u-draw_h/2u;
 canvas.blit(dest_x,dest_y,draw_w,draw_h,
   decoded_photo.pixels,src_w,src_h);
 const char*origin=detail_index<media_review.count?
   std::strrchr(media_review.entries[detail_index].preview_path,'.'):nullptr;
 const bool hdr_twin=origin&&(origin[1]=='j'||origin[1]=='J')&&
   (origin[2]=='x'||origin[2]=='X')&&(origin[3]=='r'||origin[3]=='R')&&
   origin[4]==0;
 footer(canvas,hdr_twin?
  "JXR: VERSION SDR JPG ASSOCIEE  L1 R1 PHOTO  CERCLE RETOUR":
  "L1 R1 PHOTO  L2 R2 ZOOM  CERCLE RETOUR  LECTURE SEULE");
}
void show_video_info(ps5::demo::Canvas&canvas) noexcept {
 if(task_busy()){
  panel(canvas,361,"VIDEO","LECTURE DES METADONNEES MP4/WEBM",Color::cyan);
  footer(canvas,"CERCLE ANNULER  AUCUN DECODAGE VIDEO");
  return;
 }
 if(task_cancelled()||task_failed()||!video_info_ready){
  panel(canvas,361,"VIDEO NON ANALYSABLE",
   "FORMAT OU DROITS INCOMPATIBLES",
   Color::yellow);
  panel(canvas,510,"LECTURE VIDEO / HDR","NON ENCORE PRIS EN CHARGE");
  footer(canvas,"CERCLE RETOUR  AUCUNE MODIFICATION");
  return;
 }
 char length[105]{};
 const unsigned long long seconds=video_info.duration_ms/1000ULL;
 (void)std::snprintf(length,sizeof(length),"%02llu:%02llu:%02llu",
   seconds/3600ULL,(seconds/60ULL)%60ULL,seconds%60ULL);
 char geometry[105]{};
 (void)std::snprintf(geometry,sizeof(geometry),"%u X %u PIXELS",
   video_info.width,video_info.height);
 panel(canvas,355,"DUREE MP4",length,Color::cyan);
 panel(canvas,503,"RESOLUTION",geometry,Color::cyan);
 panel(canvas,651,"DIAGNOSTIC","INDEX MP4/WEBM EN LECTURE SEULE");
 panel(canvas,799,"LECTURE VIDEO","NON DISPONIBLE POUR LE MOMENT",Color::yellow);
 footer(canvas,"CERCLE RETOUR  AUCUN DECODAGE  AUCUNE ECRITURE");
}
void show_official(ps5::demo::Canvas&canvas) noexcept {
 static constexpr const char*steps[8]={
  "SAUVEGARDER LES DONNEES","VERIFIER LES CAPTURES",
  "REVOIR LES HOMEBREWS","REVOIR LE CHARGEMENT AUTO",
  "VERIFIER LES INSTALLATIONS","REVOIR LES PARAMETRES",
  "EVALUER LA MISE A JOUR","COMPRENDRE LES RISQUES PSN"
 };
 unsigned done=0;
 for(bool checked:official_checked)if(checked)done++;
 char count[96]{};
 (void)std::snprintf(count,sizeof(count),"%u / 8 REVUS   MODE MANUEL",done);
 canvas.text(112,303,count,3,Color::yellow);
 const unsigned page=official_selection/4u;
 for(unsigned slot=0;slot<4;slot++){
  const unsigned index=page*4+slot;
  const unsigned top=351+slot*142u;
  const bool selected_item=index==official_selection;
  canvas.rectangle(95,top,1730,116,selected_item?Color::cyan:Color::panel);
  canvas.rectangle(103,top+8,1714,100,Color::background);
  canvas.text(135,top+29,steps[index],4,selected_item?Color::cyan:Color::white);
  canvas.text(1400,top+36,official_checked[index]?"REVU":"A VERIFIER",3,
    official_checked[index]?Color::cyan:Color::yellow);
 }
 footer(canvas,"X MARQUER REVU  CARRE AUTOSTART  CERCLE  AUCUNE GARANTIE PSN");
}
void show_autoload(ps5::demo::Canvas&canvas) noexcept {
 if(task_busy()){
  panel(canvas,355,"AUTOLOAD PS5","EXAMEN DES CONFIGURATIONS",Color::cyan);
  panel(canvas,503,"ACCES","LECTURE SEULE  AUCUN CHANGEMENT");
  footer(canvas,"CERCLE ANNULER  AUCUNE ACTION PSN");
  return;
 }
 if(task_failed()||task_cancelled()||!autoload_checked){
  panel(canvas,355,"AUTOLOAD INDISPONIBLE",
    task_cancelled()?"CONTROLE ANNULE":"SCAN OU PERMISSIONS LIMITES",Color::yellow);
  footer(canvas,"X REESSAYER  CERCLE RETOUR");
  return;
 }
 char detail[130]{};
 (void)std::snprintf(detail,sizeof(detail),"%u / %u CHEMINS  %u ABSENTS",
  autoload_stats.present,autoload_stats.checked,autoload_stats.missing);
 panel(canvas,355,"CONFIGURATIONS VISIBLES",detail,Color::cyan);
 (void)std::snprintf(detail,sizeof(detail),"%u ETAHEN  %u AUTRES PAYLOADS",
  autoload_stats.etahen_references,autoload_stats.other_references);
 panel(canvas,503,"REFERENCES AUTOLOAD",detail,Color::cyan);
 (void)std::snprintf(detail,sizeof(detail),"%u ACTIFS  %u DESACTIVES  %u INCONNUS",
  autoload_stats.autostart_enabled,autoload_stats.autostart_disabled,
  autoload_stats.unknown);
 panel(canvas,651,"DECLARATIONS TOOLBOX",detail,Color::yellow);
 (void)std::snprintf(detail,sizeof(detail),"%u ILLISIBLES  %u TROP GRANDS",
  autoload_stats.unreadable,autoload_stats.oversized);
 panel(canvas,799,"VERIFICATION PARTIELLE",detail,Color::yellow);
 canvas.rectangle(95,950,1730,96,Color::panel);
 canvas.text(125,963,"X RECONTROLER  CERCLE GUIDE RETOUR OFFICIEL",3,Color::white);
 canvas.text(125,1005,"REFERENCES SEULEMENT  EXECUTION NON PROUVEE  AUCUNE ECRITURE",3,Color::cyan);
}
void scene(ps5::demo::Canvas&canvas) noexcept {
 poll_pad();
 release_inactive_previews();
 const char*subtitle=screen==Screen::menu?"MAINTENANCE PS5":
   screen==Screen::analyze?"ANALYSE DE L'ENVIRONNEMENT":
   screen==Screen::access?"PERMISSIONS DU TITRE":
   screen==Screen::apps?"HOMEBREWS ET PLUGINS":
   screen==Screen::cleanup?"NETTOYAGE PROTEGE":
   screen==Screen::review?"CATALOGUE DES CAPTURES":
   screen==Screen::search?"RECHERCHE DES CAPTURES":
   screen==Screen::games?"CAPTURES PAR JEU":
   screen==Screen::detail?"DETAIL DE LA CAPTURE":
   screen==Screen::photo?"APERCU PHOTO":
   screen==Screen::video_info?"INFORMATIONS VIDEO":
   screen==Screen::autoload?"AUTOLOAD ET DEMARRAGE":
   screen==Screen::companions?"FICHIERS LIES A LA CAPTURE":"RETOUR AU SYSTEME OFFICIEL";
 header(canvas,subtitle);
 switch(screen){
  case Screen::menu:show_menu(canvas);break;
  case Screen::analyze:show_analyze(canvas);break;
  case Screen::access:show_access(canvas);break;
  case Screen::apps:show_apps(canvas);break;
  case Screen::cleanup:show_cleanup(canvas);break;
  case Screen::review:show_review(canvas);break;
  case Screen::search:show_search(canvas);break;
  case Screen::games:show_games(canvas);break;
  case Screen::detail:show_detail(canvas);break;
  case Screen::photo:show_photo(canvas);break;
  case Screen::video_info:show_video_info(canvas);break;
  case Screen::official:show_official(canvas);break;
  case Screen::autoload:show_autoload(canvas);break;
  case Screen::companions:show_companions(canvas);break;
 }
}
}

int main(){
 (void)sceUserServiceInitialize(nullptr);
 int user=-1;
 if(sceUserServiceGetInitialUser(&user)==0){
  (void)scePadInit();
  controller=scePadOpen(user,0,0,nullptr);
 }
 ps5::demo::run(scene,"CLEANMYPS5 NATIVE");
}
