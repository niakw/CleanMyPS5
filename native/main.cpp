// CleanMyPS5 native dashboard (GPL-3.0-or-later).
// Real PS5 title: reads app-visible paths locally, without opening a browser.
// Advanced diagnostics require the separately privileged, read-only helper.
#include "demo_renderer.hpp"
#include "loopback_protocol.hpp"
#include "overview_protocol.hpp"
#include "inventory.h"
#include "duplicates.h"
#include "homebrew.h"
#include "game_images.h"
#include "recovery_inventory.h"
#include "recovery_resume.hpp"
#include "compression_bridge.hpp"
#include "compression_stream.hpp"
#include "compression_preflight.hpp"
#include "compression_progress.hpp"
#include "compression_image.hpp"
#include "privilege_contract.hpp"
#include "cleanup_readiness.hpp"
#include "capture_companions.h"
#include "private_media_backup.hpp"
#include "private_group_backup.hpp"
#include "private_capsule_inventory.hpp"
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
#include <unistd.h>

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
constexpr int row_count=5;
enum class Screen { menu, analyze, access, apps, cleanup, review, search, games, detail, photo, video_info, official, autoload, companions, compression, backup, capsules, group_backup, rehearsal };
enum class Link { unchecked, ok, offline };
constexpr std::size_t core_access_count=6u;
constexpr std::size_t local_path_count=22u;
constexpr unsigned access_page_count=(local_path_count+3u)/4u;
const char* const local_paths[local_path_count]={
 "/app0/sce_sys/param.json", "/download0", "/data",
 "/user/av_contents", "/user/av_contents/photo", "/user/av_contents/video",
 "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3",
 "/mnt/usb4", "/mnt/usb5", "/mnt/usb6", "/mnt/usb7",
 "/mnt/ext0", "/mnt/ext1", "/mnt/ext2", "/mnt/ext3",
 "/mnt/ext4", "/mnt/ext5", "/mnt/ext6", "/mnt/ext7"
};
const char* const local_names[local_path_count]={
 "APP INSTALLEE", "STOCKAGE APP", "STOCKAGE HOMEBREW",
 "GALERIE PS5", "PHOTOS GALERIE", "VIDEOS GALERIE",
 "STOCKAGE USB 0", "STOCKAGE USB 1", "STOCKAGE USB 2", "STOCKAGE USB 3",
 "STOCKAGE USB 4", "STOCKAGE USB 5", "STOCKAGE USB 6", "STOCKAGE USB 7",
 "STOCKAGE EXT 0", "STOCKAGE EXT 1", "STOCKAGE EXT 2", "STOCKAGE EXT 3",
 "STOCKAGE EXT 4", "STOCKAGE EXT 5", "STOCKAGE EXT 6", "STOCKAGE EXT 7"
};
int controller=-1,selected=0;
Screen screen=Screen::menu;
std::uint32_t previous=0;
Link engine=Link::unchecked;
unsigned available=0,denied=0,absent=0,mount_unknown=0;
bool mount_reported=false;
enum class LocalAccess { unchecked, unavailable, missing, denied, opened, listable, enumeration_denied };
struct LocalProbe {
 LocalAccess status=LocalAccess::unchecked;
 unsigned entries=0;
 bool truncated=false;
 bool io_error=false;
 bool volume_known=false;
 bool capacity_only=false;
 bool mount_confirmed=false; // Different device from enclosing /mnt only
 std::uint64_t available_bytes=0;
 std::uint64_t total_bytes=0;
};
LocalProbe local_probe[local_path_count]{};
bool local_checked=false;
unsigned access_page=0;
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
/* Open each directory component relative to the PREVIOUS verified
 * descriptor. O_NOFOLLOW on a complete path protects only its last element;
 * a symlink in an intermediate directory would otherwise redirect a probe.
 * This method is read-only, works with the title sandbox permissions, and
 * intentionally rejects ambiguous "." and ".." path components. */
int open_probe_target(const char*path,bool file) noexcept {
 if(!path||path[0]!='/'){errno=EINVAL;return -1;}
 std::size_t length=0;
 while(length<256u&&path[length])++length;
 if(length<2u||length==256u||path[length-1]=='/'){
  errno=EINVAL;return -1;
 }
 int fd=open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return -1;
 const char*cursor=path+1;
 while(*cursor){
  const char*end=std::strchr(cursor,'/');
  const std::size_t part_length=end?
    static_cast<std::size_t>(end-cursor):std::strlen(cursor);
  if(part_length==0u||part_length>127u||
     (part_length==1u&&cursor[0]=='.')||
     (part_length==2u&&cursor[0]=='.'&&cursor[1]=='.')){
   (void)close(fd);errno=EINVAL;return -1;
  }
  char component[128]{};
  std::memcpy(component,cursor,part_length);
  const int flags=O_RDONLY|O_NOFOLLOW|
    (!end&&file?O_NONBLOCK:O_DIRECTORY);
  const int next=openat(fd,component,flags);
  const int error=errno;
  (void)close(fd);
  if(next<0){errno=error;return -1;}
  fd=next;
  if(!end)break;
  cursor=end+1;
 }
 return fd;
}
/* A plain /mnt/usbN directory can exist WITHOUT a mounted volume.
 * fstatvfs then reports /mnt's (or /'s) space, not an external disk.
 * Device difference is a conservative signal; equal devices are never
 * advertised as a confirmed separate capacity. */
bool mount_device_differs(const struct stat&child,
                          const struct stat&parent) noexcept {
 return S_ISDIR(child.st_mode)&&S_ISDIR(parent.st_mode)&&
        child.st_dev!=parent.st_dev;
}
bool mounted_volume_evidence(const char*path,int dir) noexcept {
 if(!path||path[0]!='/'||dir<0)return false;
 const char*last=std::strrchr(path,'/');
 if(!last||last==path||!last[1])return false;
 char parent_path[256]{};
 const std::size_t length=static_cast<std::size_t>(last-path);
 if(length>=sizeof(parent_path))return false;
 std::memcpy(parent_path,path,length);
 const int parent=open_probe_target(parent_path,false);
 if(parent<0)return false;
 struct stat child_st{},parent_st{};
 const bool known=fstat(dir,&child_st)==0&&fstat(parent,&parent_st)==0&&
                  mount_device_differs(child_st,parent_st);
 (void)close(parent);
 return known;
}
LocalProbe probe_path(const char*path,bool file,bool capacity_only=false) noexcept {
 LocalProbe result{};
 if(!path||cancellation_hook(nullptr))return result;
 // One final anchored descriptor for BOTH fstatvfs and the enumeration.
 const int fd=open_probe_target(path,file);
 if(fd<0){result.status=open_error(errno);return result;}
 if(capacity_only&&!file)
  result.mount_confirmed=mounted_volume_evidence(path,fd);
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
   if(total>0&&free<=total&&(!capacity_only||result.mount_confirmed)){
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
 if(capacity_only){
  /* The optional mount is capacity-only: never enumerate game files. */
  struct stat mount_stat{};
  const bool ok=fstat(fd,&mount_stat)==0&&S_ISDIR(mount_stat.st_mode);
  (void)close(fd);
  result.status=ok?LocalAccess::listable:LocalAccess::unavailable;
  result.capacity_only=true;
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
 /* A successful readdir EOF cannot hide a failed directory close. */
 if(closedir(dir)!=0)result.io_error=true;
 if(cancellation_hook(nullptr))return LocalProbe{}; // Ignore partial data
 return result;
}
/* The four fixed title-visible probes are NOT a global PS5 filesystem
 * inventory. Summarize only observations; never infer write privileges. */
struct LocalCoverage {
 unsigned opened=0,listable=0,missing=0,denied=0,other=0;
 unsigned entries=0;
 bool truncated=false,io_error=false;
 bool partial=true;
};
LocalCoverage summarize_local_coverage(const LocalProbe*probes,
 std::size_t count,bool checked) noexcept {
 LocalCoverage coverage{};
 if(!probes||count!=local_path_count||!checked)return coverage;
 coverage.partial=false;
 /* Optional disconnected USB roots never invalidate core gallery coverage. */
 for(std::size_t i=0;i<core_access_count;i++){
  const LocalProbe&probe=probes[i];
  switch(probe.status){
   case LocalAccess::opened: ++coverage.opened;break;
   case LocalAccess::listable: ++coverage.listable;break;
   case LocalAccess::missing: ++coverage.missing;coverage.partial=true;break;
   case LocalAccess::denied:
   case LocalAccess::enumeration_denied:
    ++coverage.denied;coverage.partial=true;break;
   default:++coverage.other;coverage.partial=true;break;
  }
  if(probe.status==LocalAccess::listable){
   coverage.entries+=probe.entries;
   coverage.truncated|=probe.truncated;
   coverage.io_error|=probe.io_error;
   coverage.partial|=probe.truncated||probe.io_error;
  }
 }
 return coverage;
}
void probe_local() noexcept {
 local_checked=false;
 // Drop previously sampled rights and capacity before every new probe.
 for(auto&sample:local_probe)sample=LocalProbe{};
 for(std::size_t i=0;i<local_path_count;i++){
  if(cancellation_hook(nullptr))return;
  local_probe[i]=probe_path(local_paths[i],i==0,i>=core_access_count);
  if(cancellation_hook(nullptr))return;
 }
 local_checked=true;
}

enum class Summary { unchecked, ok, unavailable };
enum class ScanSource { none, native_title, helper };
Summary summary_status=Summary::unchecked;
ScanSource scan_source=ScanSource::none;
/* Never merge counts observed by two processes with different permissions.
 * Helper aggregates cannot approve native media selection or cleanup. */
cmp5_native::Overview helper_summary{};
bool helper_summary_checked=false;
unsigned summary_files=0,summary_media=0,summary_cache=0,summary_logs=0;
unsigned summary_large=0,summary_old=0,summary_old_large=0;
std::uint64_t summary_bytes=0,summary_large_bytes=0;
bool summary_partial=false;
unsigned summary_gallery_photo=0,summary_gallery_video=0;
const char*gallery_label(unsigned status) noexcept {
 switch(status){
  case 1:return "OK";
  case 2:return "PARTIEL";
  case 3:return "ABSENT";
  case 4:return "INACCESSIBLE";
  case 5:return "ALIAS";
  case 6:return "NON PARCOURU";
  default:return "INCONNU";
 }
}
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
Cmp5CompanionReport companion_summary{};
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
unsigned homebrew_page=0;
Cmp5AutoloadStats autoload_stats{};
bool autoload_checked=false;
/* Coordinated read-only audit: only aggregate game counts persist here.
 * Never run compression or read full file bodies automatically. */
struct GeneralGameAudit {
 bool checked=false;
 bool partial=true;
 unsigned image_files=0,folder_games=0;
 unsigned missing_roots=0,unavailable_roots=0,scanned_roots=0;
 std::uint64_t image_nominal_bytes=0,folder_nominal_bytes=0;
 unsigned folder_partials=0,hardlinks_skipped=0;
};
GeneralGameAudit general_games{};
bool general_audit_completed=false;
/* Stage 0=idle, 1=permissions, 2=gallery, 3=apps,
 * 4=autoload, 5=games, 6=finished. UI reads only this atomic
 * while the worker publishes module results behind task_state. */
std::atomic<unsigned> general_audit_stage{0};
unsigned analyze_page=0;
unsigned cleanup_page=0;
/* The snapshot is copied before a worker starts, so a catalog rescan
 * cannot change which source file is being backed up. */
Cmp5MediaCandidate backup_selected{};
cmp5_private_backup::Outcome backup_outcome{};
unsigned backup_step=0u;
cmp5_private_backup::GroupOutcome group_outcome{};
Cmp5MediaCandidate group_primary{};
unsigned group_step=0u;
bool backup_verify_only=false;
bool backup_from_inventory=false;
/* Private backup archive can open from the home screen even if the
 * Gallery is empty/unreadable. Never route its back action via Review. */
Screen capsule_return_screen=Screen::detail;
cmp5_private_backup::Inventory private_inventory{};
cmp5_private_backup::ArchiveAudit private_archive_audit{};
cmp5_private_backup::RehearsalOutcome private_rehearsal_outcome{};
char private_rehearsal_id[32]{};
unsigned private_rehearsal_step=0u;
cmp5_private_backup::ArchiveAuditProgress private_audit_progress{};
unsigned private_inventory_selection=0;
bool private_inventory_groups=false;
bool group_verify_only=false,group_from_inventory=false;
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
 available=denied=absent=mount_unknown=0;
 mount_reported=false;
 char response[18432]{};
 const char*json=nullptr;
 if(!fetch_local("/api/preflight",response,sizeof(response),&json))return false;
 cmp5_native::Preflight parsed{};
 if(!cmp5_native::parse_preflight(json,std::strlen(json),&parsed))return false;
 available=parsed.accessible;
 denied=parsed.denied;
 absent=parsed.absent;
 mount_unknown=parsed.mount_info_unknown;
 mount_reported=parsed.mount_info_reported;
 return true;
}
bool accept_helper_overview(const char*json) noexcept {
 helper_summary=cmp5_native::Overview{};
 helper_summary_checked=false;
 if(!json)return false;
 cmp5_native::Overview sample{};
 if(!cmp5_native::parse_overview(json,std::strlen(json),&sample))return false;
 helper_summary=sample;
 helper_summary_checked=true;
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
  summary_gallery_photo=parsed.gallery_photo;
  summary_gallery_video=parsed.gallery_video;
 summary_status=Summary::ok;
 scan_source=source;
 return true;
}
void run_overview() noexcept {
 // Helper data are a separate observation, never a substitution for the
 // installed title's ability to read and select individual Gallery files.
 helper_summary=cmp5_native::Overview{};
 helper_summary_checked=false;
 // An overview rescan invalidates any older coordinated global audit.
 general_games=GeneralGameAudit{};
 general_audit_completed=false;
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
 companion_summary=Cmp5CompanionReport{};
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
  summary_gallery_photo=summary_gallery_video=0;
  scan_source=ScanSource::none;
 }
 if(cancellation_hook(nullptr))return;
 // Optionally prefer helper aggregate counters when it is already running.
 // Media review remains restricted to paths the installed title can see.
 if((summary_status!=Summary::ok||summary_partial)&&helper_is_compatible()){
  char response[2048]{};
  const char*remote=nullptr;
  if(fetch_local("/api/overview",response,sizeof(response),&remote))
   (void)accept_helper_overview(remote);
 }
}
void run_access_check() noexcept {
 probe_local();
 if(cancellation_hook(nullptr))return;
 engine=Link::offline;
 available=denied=absent=mount_unknown=0;
 mount_reported=false;
 /* A read-only helper is a separate process. X rechecks its protocol and
  * live path evidence; a successful loader send alone is never enough. */
 if(helper_is_compatible()&&!cancellation_hook(nullptr)&&get_preflight())
  engine=Link::ok;
}
void run_homebrew_check() noexcept;
void run_autoload_check() noexcept;
void run_general_games_check() noexcept;
void run_environment_check() noexcept {
 general_audit_completed=false;
 general_audit_stage.store(1u,std::memory_order_release);
 run_access_check();
 if(cancellation_hook(nullptr))return;
 general_audit_stage.store(2u,std::memory_order_release);
 run_overview();
 if(cancellation_hook(nullptr))return;
 general_audit_stage.store(3u,std::memory_order_release);
 run_homebrew_check();
 if(cancellation_hook(nullptr))return;
 general_audit_stage.store(4u,std::memory_order_release);
 run_autoload_check();
 if(cancellation_hook(nullptr))return;
 general_audit_stage.store(5u,std::memory_order_release);
 run_general_games_check();
 if(!cancellation_hook(nullptr)){
  general_audit_completed=true;
  general_audit_stage.store(6u,std::memory_order_release);
 }
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
 companion_summary=Cmp5CompanionReport{};
 companion_checked=false;
 if(detail_index>=media_review.count)return;
 const auto&primary=media_review.entries[detail_index];
 if((primary.media_type!=1u&&primary.media_type!=2u)||
    !primary.preview_path[0]||cancellation_hook(nullptr))return;
 Cmp5CompanionReport observed{};
 const bool success=cmp5_inspect_capture_companions("/", &primary,&observed,
     cancellation_hook,nullptr)!=0;
 /* Expose only a privacy-safe reason code on failure; all counts stay zero. */
 if(!cancellation_hook(nullptr)){
  companion_summary=observed;
  companion_checked=success;
 }
}
void run_private_backup() noexcept {
 backup_outcome={};
 if(!cmp5_privilege::allows(cmp5_privilege::Actor::native_title,
      cmp5_privilege::Operation::backup_private_media))return;
 backup_outcome=cmp5_private_backup::backup_one(
   backup_selected,cancellation_hook,nullptr);
}
void run_private_group_backup() noexcept {
 group_outcome={};
 if(!cmp5_privilege::allows(cmp5_privilege::Actor::native_title,
     cmp5_privilege::Operation::backup_private_media))return;
 group_outcome=cmp5_private_backup::backup_visible_group(
     group_primary,cancellation_hook,nullptr);
}
void run_private_group_verify() noexcept {
 group_outcome={};
 if(!cmp5_privilege::allows(cmp5_privilege::Actor::native_title,
      cmp5_privilege::Operation::verify_private_media))return;
 if(private_inventory_selection>=private_inventory.group_count)return;
 group_outcome=cmp5_private_backup::verify_saved_group(
    private_inventory.group_ids[private_inventory_selection],
    cancellation_hook,nullptr);
}
void run_private_backup_verify() noexcept {
 backup_outcome={};
 if(!cmp5_privilege::allows(cmp5_privilege::Actor::native_title,
      cmp5_privilege::Operation::verify_private_media))return;
 if(backup_from_inventory)
  backup_outcome=cmp5_private_backup::verify_by_id(
   private_inventory.ids[private_inventory_selection],
   cancellation_hook,nullptr);
 else
  backup_outcome=cmp5_private_backup::verify_existing(
   backup_selected,cancellation_hook,nullptr);
}
void run_private_rehearsal() noexcept {
 private_rehearsal_outcome={};
 if(!cmp5_privilege::allows(cmp5_privilege::Actor::native_title,
      cmp5_privilege::Operation::rehearse_private_media))return;
 private_rehearsal_outcome=cmp5_private_backup::rehearse_by_id(
    private_rehearsal_id,cancellation_hook,nullptr);
}
void run_capsule_inventory() noexcept {
 private_inventory=cmp5_private_backup::Inventory{};
 private_archive_audit=cmp5_private_backup::ArchiveAudit{};
 private_audit_progress.reset();
 private_inventory_selection=0u;
 cmp5_private_backup::Inventory checked{};
 if(cmp5_private_backup::scan(&checked,cancellation_hook,nullptr)&&
    !cancellation_hook(nullptr))
  private_inventory=checked;
}
void run_private_archive_audit() noexcept {
 private_archive_audit=cmp5_private_backup::ArchiveAudit{};
 private_audit_progress.reset();
 cmp5_private_backup::ArchiveAudit verified{};
 cmp5_private_backup::Inventory current{};
 if(cmp5_private_backup::audit(&verified,&current,cancellation_hook,nullptr,
                               &private_audit_progress)&&
    !cancellation_hook(nullptr)){
  /* Show exactly the bounded set independently re-scanned after hashing;
   * an old selection index must not target a different archive. */
  private_inventory=current;
  private_inventory_selection=0u;
  private_archive_audit=verified;
 }else{
  /* A workspace switch or unreadable scan invalidates the old catalogue;
   * a failed audit must not leave a seemingly current archive list. */
  private_inventory=cmp5_private_backup::Inventory{};
  private_inventory_selection=0u;
 }
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
 char*report=cmp5_diagnostics_with_stats_cancel(
   "/",&length,&autoload_stats,cancellation_hook,nullptr);
 if(report){
  std::free(report);
  autoload_checked=!cancellation_hook(nullptr)&&autoload_stats.checked==11u;
 }
}
void run_general_games_check() noexcept {
 general_games=GeneralGameAudit{};
 Cmp5GameImageStats found{};
 if(cmp5_scan_game_images("/",&found,cancellation_hook,nullptr)&&
    !cancellation_hook(nullptr)){
  unsigned images=0;
  bool valid=true;
  for(unsigned i=0;i<CMP5_IMAGE_FORMATS;i++){
   if(UINT32_MAX-images<found.count[i]){valid=false;break;}
   images+=found.count[i];
  }
  if(valid){
   general_games.image_files=images;
   general_games.folder_games=found.folder_games;
   general_games.scanned_roots=found.scanned_roots;
   general_games.missing_roots=found.missing_roots;
   general_games.unavailable_roots=found.unavailable_roots;
   general_games.image_nominal_bytes=found.nominal_bytes;
   general_games.folder_nominal_bytes=found.folder_nominal_bytes;
   general_games.folder_partials=found.folder_partials;
   general_games.hardlinks_skipped=found.hardlinks_skipped;
   general_games.partial=found.partial!=0u||
     found.unavailable_roots!=0u||found.folder_partials!=0u;
   general_games.checked=true;
  }
 }
 /* Do not persist game filepaths from the general inventory. */
 std::memset(&found,0,sizeof(found));
}
Cmp5GameImageStats game_images{};
bool game_images_checked=false;
unsigned compression_view=0,compression_selection=0,compression_destination=0;
/* Monitor is read-only. Circle restores its originating tab. */
unsigned compression_monitor_return_view=0;
unsigned compression_adjacent_tab(unsigned from,bool forward) noexcept {
 switch(from){
  case 0u:return forward?1u:6u;
  case 1u:return forward?6u:0u;
  case 6u:return forward?0u:1u;
  default:return 0u;
 }
}
unsigned compression_image_selection=0;
constexpr const char*game_destinations[]={
 "/data/homebrew",
 "/mnt/usb0/homebrew","/mnt/ext0/homebrew",
 "/mnt/usb1/homebrew","/mnt/ext1/homebrew",
 "/mnt/usb2/homebrew","/mnt/ext2/homebrew",
 "/mnt/usb3/homebrew","/mnt/ext3/homebrew",
 "/mnt/usb4/homebrew","/mnt/ext4/homebrew",
 "/mnt/usb5/homebrew","/mnt/ext5/homebrew",
 "/mnt/usb6/homebrew","/mnt/ext6/homebrew",
 "/mnt/usb7/homebrew","/mnt/ext7/homebrew"
};
constexpr unsigned game_destination_count=sizeof(game_destinations)/sizeof(game_destinations[0]);
cmp5_compression::GamePreflight game_preflight{};
bool game_preflight_checked=false,game_preflight_transport=false;
bool game_job_attempted=false,game_job_accepted=false,game_job_reply_unknown=false;
bool game_job_plan_changed=false;
unsigned game_job_position=0;
cmp5_compression::GameProgress game_progress{};
cmp5_compression::ProgressRefresh game_progress_refresh{};
std::uint64_t compression_monotonic_ns() noexcept {
 struct timespec ts{};
 if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0||ts.tv_sec<0||ts.tv_nsec<0||
    ts.tv_nsec>=1000000000L)return 0;
 const auto seconds=static_cast<std::uint64_t>(ts.tv_sec);
 if(seconds>(UINT64_MAX-static_cast<std::uint64_t>(ts.tv_nsec))/1000000000ULL)
  return 0;
 return seconds*1000000000ULL+static_cast<std::uint64_t>(ts.tv_nsec);
}
bool game_progress_checked=false;
Cmp5RecoveryInventory recovery_inventory{};
bool recovery_checked=false;
unsigned recovery_selection=0;
Cmp5RecoveryCandidate recovery_chosen{};
bool recovery_resume_attempted=false,recovery_resume_accepted=false;
bool recovery_resume_unknown=false;
bool game_pause_attempted=false,game_pause_accepted=false,game_pause_unknown=false;
bool game_resume_attempted=false,game_resume_accepted=false,game_resume_unknown=false;
char game_source_checked[192]{},game_dest_checked[48]{};
bool game_source_is_image=false;
std::uint64_t game_image_expected_bytes=0;
char game_smp_title[16]{},game_image_type[16]{};
void reset_compression_job_state() noexcept {
 game_preflight_checked=false;game_preflight_transport=false;
 game_job_attempted=false;game_job_accepted=false;game_job_reply_unknown=false;
 game_job_plan_changed=false;
 game_job_position=0;game_progress={};game_progress_checked=false;
 game_pause_attempted=false;game_pause_accepted=false;game_pause_unknown=false;
 game_resume_attempted=false;game_resume_accepted=false;game_resume_unknown=false;
 game_preflight=cmp5_compression::GamePreflight{};
 game_smp_title[0]=0;game_image_type[0]=0;
}
bool stage_game_image_preflight() noexcept {
 if(!game_images_checked||compression_image_selection>=game_images.shown_images||
    compression_destination>=game_destination_count)return false;
 const Cmp5GameImage&source=game_images.image[compression_image_selection];
 /* PFSC is already compressed, and no filename alone is trusted as an
  * SMP title identifier. SMP must resolve an actual managed image. */
 if(source.format==CMP5_IMAGE_FFPFSC||source.nominal_bytes==0||
    !source.path[0]||std::strlen(source.path)>=sizeof(game_source_checked))
  return false;
 const char*dest=game_destinations[compression_destination];
 if(std::strlen(dest)>=sizeof(game_dest_checked))return false;
 (void)std::snprintf(game_source_checked,sizeof(game_source_checked),"%s",source.path);
 (void)std::snprintf(game_dest_checked,sizeof(game_dest_checked),"%s",dest);
 reset_compression_job_state();
 game_source_is_image=true;
 game_image_expected_bytes=source.nominal_bytes;
 return true;
}
bool stage_game_preflight() noexcept {
 if(!game_images_checked||compression_selection>=game_images.shown_folders||
    compression_destination>=game_destination_count)return false;
 const char*source=game_images.folder[compression_selection].path;
 const char*dest=game_destinations[compression_destination];
 if(!source[0]||std::strlen(source)>=sizeof(game_source_checked)||
    std::strlen(dest)>=sizeof(game_dest_checked))return false;
 (void)std::snprintf(game_source_checked,sizeof(game_source_checked),"%s",source);
 (void)std::snprintf(game_dest_checked,sizeof(game_dest_checked),"%s",dest);
 reset_compression_job_state();
 game_source_is_image=false;
 game_image_expected_bytes=0;
 return true;
}
bool compression_engine_seen=false;
char compression_engine_version[32]{};
/* The GPL AMPR/LZ4 packer is bundled as an ELF INSIDE CleanMyPS5.
 * Its customized, isolated listener uses 6973, loopback only.
 * A compatible reply does not authenticate the process. */
bool check_local_ampr(char version[32]) noexcept {
 version[0]=0;
 const int socket=sceNetSocket("cmp5_ampr_probe",2,1,6);
 if(socket<0)return false;
 const int timeout_us=350000;
 (void)sceNetSetsockopt(socket,0xffff,0x1105,&timeout_us,sizeof(timeout_us));
 (void)sceNetSetsockopt(socket,0xffff,0x1106,&timeout_us,sizeof(timeout_us));
 const NetSockaddrIn local{16,2,
  static_cast<std::uint16_t>((6973u<<8)|(6973u>>8)),0x0100007f,0,{0}};
 bool ok=sceNetConnect(socket,&local,sizeof(local))>=0;
 constexpr char request[]=
  "GET /api/hello HTTP/1.0\r\nHost: 127.0.0.1:6973\r\nConnection: close\r\n\r\n";
 std::size_t sent=0,received=0;
 while(ok&&sent<sizeof(request)-1u){
  if(cancellation_hook(nullptr)){ok=false;break;}
  const int n=sceNetSend(socket,request+sent,sizeof(request)-1u-sent,0);
  if(n<=0){ok=false;break;}
  sent+=static_cast<std::size_t>(n);
 }
 char response[1024]{};
 while(ok&&received<sizeof(response)-1u){
  if(cancellation_hook(nullptr)){ok=false;break;}
  const int n=sceNetRecv(socket,response+received,
                         sizeof(response)-1u-received,0);
  if(n<0){ok=false;break;}
  if(n==0)break;
  received+=static_cast<std::size_t>(n);
 }
 (void)sceNetSocketClose(socket);
 if(!ok||received>=sizeof(response)-1u||
    cancellation_hook(nullptr))return false;
 cmp5_compression::Hello hello{};
 if(!cmp5_compression::parse_hello_http(response,received,&hello)||
    std::strcmp(hello.version,"CleanMyPS5-1.0")!=0)
  return false;
 std::memcpy(version,hello.version,sizeof(hello.version));
 return true;
}
enum class EngineLaunch { untouched, active, missing_file, invalid_elf, loader_unavailable, send_failed, submitted };
EngineLaunch engine_launch=EngineLaunch::untouched;
void run_embedded_engine() noexcept {
 engine_launch=EngineLaunch::untouched;
 if(!cmp5_privilege::allows(cmp5_privilege::Actor::native_title,
                           cmp5_privilege::Operation::request_compression))
  return;
 char version[32]{};
 if(check_local_ampr(version)){
  compression_engine_seen=true;
  std::memcpy(compression_engine_version,version,sizeof(version));
  engine_launch=EngineLaunch::active;
  return;
 }
 /* Only the bundled title-owned asset may be sent, never an external
  * pathname, untrusted game file, network download or user-chosen ELF. */
 const int fd=open_probe_target("/app0/assets/cleanmyps5-compression.elf",true);
 if(fd<0){engine_launch=EngineLaunch::missing_file;return;}
 struct stat st{};
 unsigned char magic[64]{};
 ssize_t got=read(fd,magic,sizeof(magic));
 if(fstat(fd,&st)!=0||!S_ISREG(st.st_mode)||st.st_nlink!=1||
    st.st_size<4096||st.st_size>64LL*1024*1024||
    got!=static_cast<ssize_t>(sizeof(magic))||
    !cmp5_compression::elf_header(magic,sizeof(magic))||
    lseek(fd,0,SEEK_SET)!=0){
  (void)close(fd);engine_launch=EngineLaunch::invalid_elf;return;
 }
 const int socket=sceNetSocket("cmp5_embedded_loader",2,1,6);
 if(socket<0){(void)close(fd);engine_launch=EngineLaunch::loader_unavailable;return;}
 const int timeout_us=5000000;
 (void)sceNetSetsockopt(socket,0xffff,0x1105,&timeout_us,sizeof(timeout_us));
 (void)sceNetSetsockopt(socket,0xffff,0x1106,&timeout_us,sizeof(timeout_us));
 const NetSockaddrIn loader{16,2,
  static_cast<std::uint16_t>((9021u<<8)|(9021u>>8)),0x0100007f,0,{0}};
 if(sceNetConnect(socket,&loader,sizeof(loader))<0){
  (void)sceNetSocketClose(socket);(void)close(fd);
  engine_launch=EngineLaunch::loader_unavailable;return;
 }
 std::uint64_t sent=0;
 const bool ok=cmp5_compression::stream_embedded_elf(fd,
  [socket](const unsigned char*bytes,std::size_t size) noexcept{
   return sceNetSend(socket,bytes,size,0);
  },[]{return cancellation_hook(nullptr)!=0;},&sent);
 (void)close(fd);
 (void)sceNetSocketClose(socket);
 engine_launch=ok&&sent>=4096?EngineLaunch::submitted:EngineLaunch::send_failed;
 /* A successful TCP send is NOT equivalent to an ELF having loaded.
  * The user can recheck via X after the jailbreak loader accepts it. */
}
/* Optional, manually started diagnostic process. This is NOT a
 * credentials elevation for the installed title. Its API is read-only. */
EngineLaunch helper_launch=EngineLaunch::untouched;
void run_embedded_readonly_helper() noexcept {
 helper_launch=EngineLaunch::untouched;
 if(helper_is_compatible()){helper_launch=EngineLaunch::active;return;}
 const int fd=open_probe_target("/app0/assets/cleanmyps5-helper.elf",true);
 if(fd<0){helper_launch=EngineLaunch::missing_file;return;}
 struct stat st{};
 unsigned char magic[64]{};
 const ssize_t got=read(fd,magic,sizeof(magic));
 if(fstat(fd,&st)!=0||!S_ISREG(st.st_mode)||st.st_nlink!=1||
    st.st_size<4096||st.st_size>64LL*1024*1024||
    got!=static_cast<ssize_t>(sizeof(magic))||
    !cmp5_compression::elf_header(magic,sizeof(magic))||
    lseek(fd,0,SEEK_SET)!=0){
  (void)close(fd);helper_launch=EngineLaunch::invalid_elf;return;
 }
 const int sock=sceNetSocket("cmp5_readonly_helper_loader",2,1,6);
 if(sock<0){(void)close(fd);helper_launch=EngineLaunch::loader_unavailable;return;}
 const int timeout_us=5000000;
 (void)sceNetSetsockopt(sock,0xffff,0x1105,&timeout_us,sizeof(timeout_us));
 (void)sceNetSetsockopt(sock,0xffff,0x1106,&timeout_us,sizeof(timeout_us));
 const NetSockaddrIn loader{16,2,
  static_cast<std::uint16_t>((9021u<<8)|(9021u>>8)),0x0100007f,0,{0}};
 if(sceNetConnect(sock,&loader,sizeof(loader))<0){
  (void)sceNetSocketClose(sock);(void)close(fd);
  helper_launch=EngineLaunch::loader_unavailable;return;
 }
 std::uint64_t sent=0;
 const bool ok=cmp5_compression::stream_embedded_elf(fd,
  [sock](const unsigned char*bytes,std::size_t size) noexcept{
   return sceNetSend(sock,bytes,size,0);
  },[]{return cancellation_hook(nullptr)!=0;},&sent);
 (void)close(fd);
 (void)sceNetSocketClose(sock);
 helper_launch=ok&&sent>=4096?EngineLaunch::submitted:EngineLaunch::send_failed;
 /* Success here only means bytes reached elfldr, not helper availability.
  * Only a separate verified /api/capabilities check identifies the service. */
}
/* A filename alone cannot identify a PS5 game. Ask the bundled engine to
 * resolve a managed image using ShadowMountPlus and return strict metadata. */
bool resolve_managed_image(const char*source,
                           cmp5_compression::ImageResolution*out) noexcept {
 char encoded[600]{};
 if(!out||!cmp5_compression::form_encode_path(source,encoded,sizeof(encoded)))
  return false;
 char request[900]{};
 const int size=std::snprintf(request,sizeof(request),
  "GET /api/cmp5-image?path=%s HTTP/1.0\r\n"
  "Host: 127.0.0.1:6973\r\nConnection: close\r\n\r\n",encoded);
 if(size<=0||static_cast<std::size_t>(size)>=sizeof(request))return false;
 const int fd=sceNetSocket("cmp5_ampr_image_resolve",2,1,6);
 if(fd<0)return false;
 const int timeout_us=15000000;
 (void)sceNetSetsockopt(fd,0xffff,0x1105,&timeout_us,sizeof(timeout_us));
 (void)sceNetSetsockopt(fd,0xffff,0x1106,&timeout_us,sizeof(timeout_us));
 const NetSockaddrIn local{16,2,
  static_cast<std::uint16_t>((6973u<<8)|(6973u>>8)),0x0100007f,0,{0}};
 bool ok=sceNetConnect(fd,&local,sizeof(local))>=0;
 std::size_t sent=0;
 while(ok&&sent<static_cast<std::size_t>(size)){
  if(cancellation_hook(nullptr)){ok=false;break;}
  int n=sceNetSend(fd,request+sent,static_cast<std::size_t>(size)-sent,0);
  if(n<=0){ok=false;break;}
  sent+=static_cast<std::size_t>(n);
 }
 char response[1024]{};
 std::size_t used=0;
 while(ok&&used<sizeof(response)){
  if(cancellation_hook(nullptr)){ok=false;break;}
  int n=sceNetRecv(fd,response+used,sizeof(response)-used,0);
  if(n<0){ok=false;break;}
  if(n==0)break;
  used+=static_cast<std::size_t>(n);
 }
 (void)sceNetSocketClose(fd);
 if(!ok||used==sizeof(response)||cancellation_hook(nullptr))return false;
 cmp5_compression::HttpJson json{};
 return cmp5_compression::parse_ampr_http(response,used,&json)&&
  cmp5_compression::parse_image_resolution(json.body,json.size,out);
}
/* Bounded individual game check via our embedded PS5 LZ4 engine. */
bool game_check_request(const char*source,const char*dest,const char*smp,
                        cmp5_compression::GamePreflight*out) noexcept {
 char game_arg[600]{},dest_arg[200]{};
 if(!cmp5_compression::form_encode_path(source,game_arg,sizeof(game_arg))||
    !cmp5_compression::form_encode_path(dest,dest_arg,sizeof(dest_arg)))
  return false;
 char smp_arg[36]{};
 if(smp&&smp[0]){
  if(std::strlen(smp)!=9u)return false;
  for(unsigned i=0;i<9;i++){
   const char c=smp[i];
   if(i<4?(c<'A'||c>'Z'):(c<'0'||c>'9'))return false;
  }
  (void)std::snprintf(smp_arg,sizeof(smp_arg),"&smp=%s",smp);
 }
 char request[1024]{};
 const int size=std::snprintf(request,sizeof(request),
   "GET /api/check?path=%s&dest=%s%s HTTP/1.0\r\n"
   "Host: 127.0.0.1:6973\r\nConnection: close\r\n\r\n",
   game_arg,dest_arg,smp_arg);
 if(size<=0||static_cast<std::size_t>(size)>=sizeof(request))return false;
 const int socket=sceNetSocket("cmp5_ampr_game_check",2,1,6);
 if(socket<0)return false;
 const int timeout_us=10000000;
 (void)sceNetSetsockopt(socket,0xffff,0x1105,&timeout_us,sizeof(timeout_us));
 (void)sceNetSetsockopt(socket,0xffff,0x1106,&timeout_us,sizeof(timeout_us));
 const NetSockaddrIn local{16,2,
   static_cast<std::uint16_t>((6973u<<8)|(6973u>>8)),0x0100007f,0,{0}};
 bool ok=sceNetConnect(socket,&local,sizeof(local))>=0;
 std::size_t sent=0;
 while(ok&&sent<static_cast<std::size_t>(size)){
  if(cancellation_hook(nullptr)){ok=false;break;}
  int n=sceNetSend(socket,request+sent,
      static_cast<std::size_t>(size)-sent,0);
  if(n<=0){ok=false;break;}
  sent+=static_cast<std::size_t>(n);
 }
 char response[8192]{};
 std::size_t used=0;
 while(ok&&used<sizeof(response)){
  if(cancellation_hook(nullptr)){ok=false;break;}
  const int n=sceNetRecv(socket,response+used,sizeof(response)-used,0);
  if(n<0){ok=false;break;}
  if(n==0)break;
  used+=static_cast<std::size_t>(n);
 }
 (void)sceNetSocketClose(socket);
 if(!ok||used==sizeof(response)||cancellation_hook(nullptr))return false;
 cmp5_compression::HttpJson http{};
 return cmp5_compression::parse_ampr_http(response,used,&http)&&
   cmp5_compression::parse_game_preflight(http.body,http.size,out);
}
/* Final explicit job submission. The title NEVER asks to overwrite,
 * delete, force conversion or operate on an unreviewed path. */
bool ampr_submit_job(const char*source,const char*dest,const char*smp,
                     std::uint64_t expected_image_bytes,
                     cmp5_compression::StartAck*out,
                     bool*could_have_started) noexcept {
 if(could_have_started)*could_have_started=false;
 if(!cmp5_privilege::allows(cmp5_privilege::Actor::native_title,
                           cmp5_privilege::Operation::request_compression))
  return false;
 char src_encoded[600]{},dst_encoded[200]{};
 if(!cmp5_compression::form_encode_path(source,src_encoded,sizeof(src_encoded))||
    !cmp5_compression::form_encode_path(dest,dst_encoded,sizeof(dst_encoded)))
  return false;
 char smp_arg[36]{};
 if(smp&&smp[0]){
  if(std::strlen(smp)!=9u)return false;
  for(unsigned i=0;i<9;i++){
   const char c=smp[i];
   if(i<4?(c<'A'||c>'Z'):(c<'0'||c>'9'))return false;
  }
  (void)std::snprintf(smp_arg,sizeof(smp_arg),"&smp=%s",smp);
 }
 char size_arg[48]{};
 if(smp&&smp[0]){
  if(expected_image_bytes==0)return false;
  const int n=std::snprintf(size_arg,sizeof(size_arg),
     "&cmp5_size=%llu",static_cast<unsigned long long>(expected_image_bytes));
  if(n<=0||static_cast<std::size_t>(n)>=sizeof(size_arg))return false;
 }
 char body[1024]{};
 const int bytes=std::snprintf(body,sizeof(body),
  "path=%s&dest=%s&level=9&workers=4&verify=1&dedup=1&"
  "overwrite=0&remove=0&force=0&clean=0%s%s",
  src_encoded,dst_encoded,smp_arg,size_arg);
 if(bytes<=0||static_cast<std::size_t>(bytes)>=sizeof(body))return false;
 char header[280]{};
 const int header_len=std::snprintf(header,sizeof(header),
  "POST /api/start HTTP/1.0\r\nHost: 127.0.0.1:6973\r\n"
  "Content-Type: application/x-www-form-urlencoded\r\n"
  "Content-Length: %d\r\nConnection: close\r\n\r\n",bytes);
 if(header_len<=0||static_cast<std::size_t>(header_len)>=sizeof(header))return false;
 const int fd=sceNetSocket("cmp5_ampr_submit",2,1,6);
 if(fd<0)return false;
 const int timeout_us=12000000;
 (void)sceNetSetsockopt(fd,0xffff,0x1105,&timeout_us,sizeof(timeout_us));
 (void)sceNetSetsockopt(fd,0xffff,0x1106,&timeout_us,sizeof(timeout_us));
 const NetSockaddrIn host{16,2,
  static_cast<std::uint16_t>((6973u<<8)|(6973u>>8)),0x0100007f,0,{0}};
 bool ok=sceNetConnect(fd,&host,sizeof(host))>=0;
 auto write_all=[fd](const char*data,std::size_t count) noexcept{
  std::size_t offset=0;
  while(offset<count){
   if(cancellation_hook(nullptr))return false;
   int n=sceNetSend(fd,data+offset,count-offset,0);
   if(n<=0)return false;
   offset+=static_cast<std::size_t>(n);
  }
  return true;
 };
 if(ok)ok=write_all(header,static_cast<std::size_t>(header_len));
 if(ok){
  // The upstream HTTP server is allowed to start converting once it
  // has the full request. After this point, an absent ACK is UNKNOWN,
  // not proof that nothing was queued: never retry automatically.
  if(could_have_started)*could_have_started=true;
  ok=write_all(body,static_cast<std::size_t>(bytes));
 }
 char response[1024]{};
 std::size_t used=0;
 while(ok&&used<sizeof(response)){
  if(cancellation_hook(nullptr)){ok=false;break;}
  int n=sceNetRecv(fd,response+used,sizeof(response)-used,0);
  if(n<0){ok=false;break;}
  if(n==0)break;
  used+=static_cast<std::size_t>(n);
 }
 (void)sceNetSocketClose(fd);
 if(!ok||used==sizeof(response))return false;
 cmp5_compression::HttpJson http{};
 return cmp5_compression::parse_ampr_http(response,used,&http)&&
   cmp5_compression::parse_start_ack(http.body,http.size,out);
}
bool ampr_read_progress(cmp5_compression::GameProgress*out) noexcept {
 if(!out)return false;
 const int socket=sceNetSocket("cmp5_ampr_progress",2,1,6);
 if(socket<0)return false;
 const int timeout_us=5000000;
 (void)sceNetSetsockopt(socket,0xffff,0x1105,&timeout_us,sizeof(timeout_us));
 (void)sceNetSetsockopt(socket,0xffff,0x1106,&timeout_us,sizeof(timeout_us));
 const NetSockaddrIn local{16,2,
  static_cast<std::uint16_t>((6973u<<8)|(6973u>>8)),0x0100007f,0,{0}};
 bool ok=sceNetConnect(socket,&local,sizeof(local))>=0;
 constexpr char request[]=
  "GET /api/cmp5-progress HTTP/1.0\r\nHost: 127.0.0.1:6973\r\n"
  "Connection: close\r\n\r\n";
 std::size_t sent=0,received=0;
 while(ok&&sent<sizeof(request)-1u){
  if(cancellation_hook(nullptr)){ok=false;break;}
  int n=sceNetSend(socket,request+sent,sizeof(request)-1u-sent,0);
  if(n<=0){ok=false;break;}
  sent+=static_cast<std::size_t>(n);
 }
 char response[2048]{};
 while(ok&&received<sizeof(response)){
  if(cancellation_hook(nullptr)){ok=false;break;}
  int n=sceNetRecv(socket,response+received,sizeof(response)-received,0);
  if(n<0){ok=false;break;}
  if(n==0)break;
  received+=static_cast<std::size_t>(n);
 }
 (void)sceNetSocketClose(socket);
 if(!ok||received==sizeof(response)||cancellation_hook(nullptr))return false;
 cmp5_compression::HttpJson body{};
 return cmp5_compression::parse_ampr_http(response,received,&body)&&
        cmp5_compression::parse_progress(body.body,body.size,out);
}
void run_game_progress() noexcept {
 game_progress_checked=false;
 cmp5_compression::GameProgress result{};
 char ver[32]{};
 if(check_local_ampr(ver)&&ampr_read_progress(&result)&&
    !cancellation_hook(nullptr)){
  game_progress=result;game_progress_checked=true;
  /* A confirmed transition back to RUNNING concludes an accepted resume.
   * A later automatic storage pause can then be resumed separately. */
  if(result.state==cmp5_compression::JobState::running &&
     game_job_accepted && game_preflight_checked &&
     std::strcmp(result.title_id,game_preflight.title_id)==0){
   game_resume_accepted=false;
   game_pause_accepted=false;
  }
 }
}
/* Pause is reversible; it checkpoints the packed output, NEVER the
 * original game directory. Refuse to pause another title's active job. */
bool ampr_safe_command(const char*route,const char*form,
                       bool*could_have_acted=nullptr) noexcept {
 if(could_have_acted)*could_have_acted=false;
 // Only two explicit, reversible actions are available in this frontend.
 if(!route||!form||
    (std::strcmp(route,"/api/pause")&&std::strcmp(route,"/api/resume")))
  return false;
 const std::size_t body_len=std::strlen(form);
 if(body_len>256u)return false;
 if((std::strcmp(route,"/api/pause")==0||
     std::strcmp(route,"/api/resume")==0)&&
    (body_len<16||std::strncmp(form,"dest=",5)!=0))return false;
 const int fd=sceNetSocket("cmp5_ampr_job_control",2,1,6);
 if(fd<0)return false;
 const int timeout_us=5000000;
 (void)sceNetSetsockopt(fd,0xffff,0x1105,&timeout_us,sizeof(timeout_us));
 (void)sceNetSetsockopt(fd,0xffff,0x1106,&timeout_us,sizeof(timeout_us));
 const NetSockaddrIn local{16,2,
  static_cast<std::uint16_t>((6973u<<8)|(6973u>>8)),0x0100007f,0,{0}};
 bool ok=sceNetConnect(fd,&local,sizeof(local))>=0;
 char header[300]{};
 const int len=std::snprintf(header,sizeof(header),
  "POST %s HTTP/1.0\r\nHost: 127.0.0.1:6973\r\n"
  "Content-Type: application/x-www-form-urlencoded\r\n"
  "Content-Length: %zu\r\nConnection: close\r\n\r\n",route,body_len);
 if(len<=0||static_cast<std::size_t>(len)>=sizeof(header))ok=false;
 const auto send_all=[fd](const char*data,std::size_t count) noexcept {
  std::size_t sent=0;
  while(sent<count){
   if(cancellation_hook(nullptr))return false;
   int n=sceNetSend(fd,data+sent,count-sent,0);
   if(n<=0)return false;
   sent+=static_cast<std::size_t>(n);
  }
  return true;
 };
 if(ok)ok=send_all(header,static_cast<std::size_t>(len));
 if(ok&&body_len){
  /* Once the request BODY may have reached the worker, a lost reply
   * does not mean rejection. Never replay the same non-idempotent
   * resume (or pause) just because the ACK was lost. */
  if(could_have_acted)*could_have_acted=true;
  ok=send_all(form,body_len);
 }
 char response[512]{};
 std::size_t used=0;
 while(ok&&used<sizeof(response)){
  if(cancellation_hook(nullptr)){ok=false;break;}
  const int n=sceNetRecv(fd,response+used,sizeof(response)-used,0);
  if(n<0){ok=false;break;}
  if(n==0)break;
  used+=static_cast<std::size_t>(n);
 }
 (void)sceNetSocketClose(fd);
 if(!ok||used==sizeof(response))return false;
 cmp5_compression::HttpJson answer{};
 constexpr char yes[]="{\"ok\":true}";
 return cmp5_compression::parse_ampr_http(response,used,&answer)&&
  answer.size==sizeof(yes)-1&&std::memcmp(answer.body,yes,answer.size)==0;
}
void run_game_pause() noexcept {
 if(game_pause_accepted||game_pause_unknown)return;
 game_pause_attempted=true;game_pause_accepted=false;
 game_resume_attempted=false;game_resume_accepted=false;
 if(!game_job_accepted||!game_preflight_checked||
    !game_preflight.ready())return;
 char ver[32]{};
 cmp5_compression::GameProgress fresh{};
 if(!check_local_ampr(ver)||!ampr_read_progress(&fresh)||
    fresh.state!=cmp5_compression::JobState::running||
    std::strcmp(fresh.title_id,game_preflight.title_id)!=0||
    cancellation_hook(nullptr))return;
 /* The engine checks this exact output against its scheduler state
  * while holding g_mx. A stale poll can no longer pause another game. */
 char body[256]{};
 if(!cmp5_compression::build_pause_body(game_dest_checked,
                                       game_preflight.title_id,
                                       body,sizeof(body)))return;
 bool could_have_acted=false;
 game_pause_accepted=ampr_safe_command("/api/pause",body,&could_have_acted);
 game_pause_unknown=!game_pause_accepted&&could_have_acted;
}
void run_game_resume() noexcept {
 /* An accepted resume is a queued mutation, not a reason to send the
  * same request again while status still reports the old PAUSED state. */
 if(game_resume_accepted||game_resume_unknown)return;
 game_resume_attempted=true;game_resume_accepted=false;
 game_pause_attempted=false;
 /* Paused jobs and explicitly interrupted jobs may have a private
  * checkpoint. Only resume the originally confirmed game and destination;
  * the PS5 worker separately revalidates its source, owner and flags. */
 if(!game_job_accepted||!game_preflight_checked||
    !game_preflight.ready())return;
 char version[32]{};
 cmp5_compression::GameProgress fresh{};
 if(!check_local_ampr(version)||!ampr_read_progress(&fresh)||
    std::strcmp(fresh.title_id,game_preflight.title_id)!=0||
    cancellation_hook(nullptr))return;
 /* The engine can checkpoint automatically when USB/external storage
  * disappears. Require the SAME previously approved title, but do not
  * require a previous controller-generated pause acknowledgment. */
 const bool paused=fresh.state==cmp5_compression::JobState::paused;
 const bool interrupted=fresh.state==cmp5_compression::JobState::error||
                        fresh.state==cmp5_compression::JobState::cancelled;
 if(!paused&&!interrupted)return;
 if(game_source_is_image){
  cmp5_compression::ImageResolution current{};
  if(!resolve_managed_image(game_source_checked,&current)||
     current.bytes!=game_image_expected_bytes||
     std::strcmp(current.title_id,game_smp_title)!=0||
     std::strcmp(current.image_type,game_image_type)!=0)return;
 }
 // Resume only a game output under a specifically authorized root.
 // A saved or stale UI value is not itself a permission grant.
 if(!cmp5_compression::allowed_destination(game_dest_checked))return;
 // Resume the fixed output created by THIS UI, never an arbitrary
 // checkpoint or source folder entered by another process.
 char dest[256]{};
 int n=std::snprintf(dest,sizeof(dest),"%s/%s",
  game_dest_checked,game_preflight.title_id);
 if(n<=0||static_cast<std::size_t>(n)>=sizeof(dest))return;
 char path[600]{};
 if(!cmp5_compression::form_encode_path(dest,path,sizeof(path)))return;
 char body[650]{};
 /* Resuming a confirmed, paused job must keep the LZ4 level
  * persisted by the engine. A per-game profile may override the level
  * used by our original level=9 start request. Omit 'level' so the
  * pinned /api/resume endpoint passes 0 to job_resume_level(). */
 n=std::snprintf(body,sizeof(body),"dest=%s",path);
 if(n<=0||static_cast<std::size_t>(n)>=sizeof(body))return;
 bool could_have_acted=false;
 game_resume_accepted=ampr_safe_command("/api/resume",body,&could_have_acted);
 game_resume_unknown=!game_resume_accepted&&could_have_acted;
}
void run_game_submission() noexcept {
 /* Never lose an accepted or ambiguous outcome by triggering the same
  * worker twice (including an accidental duplicate controller event). */
 if(game_job_accepted||game_job_reply_unknown)return;
 game_job_attempted=true;
 game_job_plan_changed=false;
 if(!game_preflight_checked||!game_preflight.ready())return;
 char ver[32]{};
 if(!check_local_ampr(ver)||cancellation_hook(nullptr))return;
 // Re-check the source, compatibility and free space immediately before
 // POST. Disallow a source which is already the would-be output path.
 cmp5_compression::GamePreflight now{};
 if(game_source_is_image){
  cmp5_compression::ImageResolution identity{};
  if(!resolve_managed_image(game_source_checked,&identity)||
     identity.bytes!=game_image_expected_bytes||
     std::strcmp(identity.title_id,game_smp_title)!=0||
     std::strcmp(identity.image_type,game_image_type)!=0)return;
 }
 if(!game_check_request(game_source_checked,game_dest_checked,
     game_source_is_image?game_smp_title:"",&now)||
    !now.ready()||
    (game_source_is_image&&now.image_bytes!=game_image_expected_bytes)||
    cancellation_hook(nullptr))return;
 if(!cmp5_compression::same_confirmation_snapshot(game_preflight,now)){
  // Never silently substitute a changed plan after two confirmations.
  // Circle returns to the candidate list for a new preflight.
  game_job_plan_changed=true;
  return;
 }
 /* Prevent requeue after a lost ACK or UI restart. A fresh preflight
  * alone cannot tell whether a conversion has already been queued.
  * If the live engine status cannot be read or has any queued/running/
  * paused task, fail closed; it could be our game or another one.
  * A completed/error status for the SAME ID also requires manual
  * inspection instead of automatic duplicate submission. */
 cmp5_compression::GameProgress existing{};
 if(!ampr_read_progress(&existing)||existing.queue_len!=0u||
    existing.state==cmp5_compression::JobState::running||
    existing.state==cmp5_compression::JobState::paused||
    (existing.state!=cmp5_compression::JobState::idle&&
      std::strcmp(existing.title_id,now.title_id)==0)||
    cancellation_hook(nullptr))return;
 char would_overwrite[256]{};
 const int n=std::snprintf(would_overwrite,sizeof(would_overwrite),
  "%s/%s",game_dest_checked,now.title_id);
 if(n<=0||static_cast<std::size_t>(n)>=sizeof(would_overwrite)||
    std::strcmp(would_overwrite,game_source_checked)==0)return;
 cmp5_compression::StartAck ack{};
 bool may_have_queued=false;
 const bool accepted=ampr_submit_job(game_source_checked,game_dest_checked,
                                     game_source_is_image?game_smp_title:"",
                                     game_source_is_image?game_image_expected_bytes:0,
                                     &ack,&may_have_queued);
 game_job_accepted=accepted;
 game_job_reply_unknown=!accepted&&may_have_queued;
 if(accepted)game_job_position=ack.position;
}
void run_game_preflight() noexcept {
 game_preflight=cmp5_compression::GamePreflight{};
 game_preflight_checked=false;game_preflight_transport=false;
 game_smp_title[0]=0;game_image_type[0]=0;
 char ver[32]{};
 if(!check_local_ampr(ver)||cancellation_hook(nullptr))return;
 if(game_source_is_image){
  cmp5_compression::ImageResolution identity{};
  if(!resolve_managed_image(game_source_checked,&identity)||
     identity.bytes!=game_image_expected_bytes)return;
  std::memcpy(game_smp_title,identity.title_id,sizeof(identity.title_id));
  std::memcpy(game_image_type,identity.image_type,sizeof(identity.image_type));
 }
 cmp5_compression::GamePreflight result{};
 if(!game_check_request(game_source_checked,game_dest_checked,
     game_source_is_image?game_smp_title:"",&result)||
    (game_source_is_image&&
       (std::strcmp(result.title_id,game_smp_title)!=0||
        result.image_bytes!=game_image_expected_bytes))||
    cancellation_hook(nullptr))return;
 game_preflight=result;
 game_preflight_transport=true;
 game_preflight_checked=true;
}
void run_recovery_inventory() noexcept {
 /* An earlier successful scan must not survive a cancelled or failed rescan
  * as a stale in-memory candidate list. */
 recovery_checked=false;
 recovery_inventory=Cmp5RecoveryInventory{};
 recovery_selection=0u;
 Cmp5RecoveryInventory sample{};
 if(cmp5_scan_recovery_inventory_cancel("/",&sample,cancellation_hook,nullptr)&&
    !cancellation_hook(nullptr)){
  recovery_inventory=sample;
  recovery_selection=0;
  recovery_checked=true;
 }
}
/* Explicit post-restart recovery: compare the chosen checkpoint against
 * a FRESH on-disk inventory and the engine's current state before POST.
 * The worker independently revalidates its source and output identity. */
void run_recovery_resume() noexcept {
 if(recovery_resume_attempted)return;
 /* A PRE-SEND refusal (engine offline, stale path, changed queue) did not
  * modify storage and permits a fresh two-step confirmation. Once BODY
  * transmission may have begun, never retry with uncertain acknowledgement. */
 recovery_resume_accepted=false;recovery_resume_unknown=false;
 if(!cmp5_compression::recovery_candidate_fresh(recovery_chosen,
      [](const char*root,Cmp5RecoveryInventory*out) noexcept {
       return cmp5_scan_recovery_inventory_cancel(root,out,
                 cancellation_hook,nullptr);
      })||cancellation_hook(nullptr))return;
 char version[32]{};
 cmp5_compression::GameProgress state{};
 if(!check_local_ampr(version)||!ampr_read_progress(&state)||
    !cmp5_compression::recovery_engine_ready(recovery_chosen,state)||
    cancellation_hook(nullptr))return;
 char form[256]{};
 if(!cmp5_compression::recovery_resume_body(recovery_chosen,
                                            form,sizeof(form)))return;
 bool could_have_acted=false;
 recovery_resume_accepted=ampr_safe_command("/api/resume",form,
                                           &could_have_acted);
 recovery_resume_unknown=!recovery_resume_accepted&&could_have_acted;
 recovery_resume_attempted=recovery_resume_accepted||could_have_acted;
}
void run_game_image_scan() noexcept {
 game_images=Cmp5GameImageStats{};
 game_images_checked=false;
 compression_engine_seen=false;
 compression_engine_version[0]=0;
 Cmp5GameImageStats sample{};
 if(cmp5_scan_game_images("/",&sample,cancellation_hook,nullptr)&&
    !cancellation_hook(nullptr)){
  game_images=sample;game_images_checked=true;
  compression_selection=0;
  compression_image_selection=0;
 }
 if(!cancellation_hook(nullptr))
  compression_engine_seen=check_local_ampr(compression_engine_version);
}
enum class Task : unsigned { none, environment, access, overview, duplicates, homebrew, autoload, photo, video_info, companions, images, engine_start, helper_start, game_check, game_start, game_progress, game_pause, game_resume, recovery_scan, recovery_resume, private_backup, private_backup_verify, private_inventory, private_group_backup, private_group_verify, private_archive_audit, private_rehearsal };
std::atomic<unsigned> task_state{0}; // 0=idle, 1=busy, 2=complete, 3=start failed, 4=cancelled
Task task_kind=Task::none;

void* background_task(void*) noexcept {
 switch(task_kind){
  case Task::environment: run_environment_check(); break;
  case Task::access: run_access_check(); break;
  case Task::overview: run_overview(); break;
  case Task::duplicates: run_duplicate_check(); break;
  case Task::homebrew: run_homebrew_check(); break;
  case Task::autoload: run_autoload_check(); break;
  case Task::photo: run_photo_preview(); break;
  case Task::video_info: run_video_info(); break;
  case Task::companions: run_companion_check(); break;
  case Task::private_backup: run_private_backup(); break;
  case Task::private_group_backup: run_private_group_backup(); break;
  case Task::private_group_verify: run_private_group_verify(); break;
  case Task::private_backup_verify: run_private_backup_verify(); break;
  case Task::private_inventory: run_capsule_inventory(); break;
  case Task::private_archive_audit: run_private_archive_audit(); break;
  case Task::private_rehearsal: run_private_rehearsal(); break;
  case Task::images: run_game_image_scan(); break;
  case Task::engine_start: run_embedded_engine(); break;
  case Task::helper_start: run_embedded_readonly_helper(); break;
  case Task::game_check: run_game_preflight(); break;
  case Task::game_start: run_game_submission(); break;
  case Task::game_progress: run_game_progress(); break;
  case Task::game_pause: run_game_pause(); break;
  case Task::game_resume: run_game_resume(); break;
  case Task::recovery_scan: run_recovery_inventory(); break;
  case Task::recovery_resume: run_recovery_resume(); break;
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
   summary_gallery_photo=summary_gallery_video=0;
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
   companion_summary=Cmp5CompanionReport{};
   companion_checked=false;
  }
  if(task_kind==Task::private_archive_audit)
   private_archive_audit=cmp5_private_backup::ArchiveAudit{};
  if(task_kind==Task::private_rehearsal)
   private_rehearsal_outcome=cmp5_private_backup::RehearsalOutcome{};
  if(task_kind==Task::images){
   game_images=Cmp5GameImageStats{};game_images_checked=false;
   compression_engine_seen=false;compression_engine_version[0]=0;
  }
  if(task_kind==Task::environment){
   general_audit_stage.store(0u,std::memory_order_release);
   general_games=GeneralGameAudit{};
   general_audit_completed=false;
   homebrew_stats=Cmp5HomebrewStats{};homebrew_checked=false;
   autoload_stats=Cmp5AutoloadStats{};autoload_checked=false;
  }
  if(task_kind==Task::engine_start)engine_launch=EngineLaunch::untouched;
  if(task_kind==Task::helper_start)helper_launch=EngineLaunch::untouched;
  if(task_kind==Task::game_check){game_preflight_checked=false;game_preflight_transport=false;game_preflight={};}
  /* A cancelled POST may have already reached the engine: keep the
   * uncertain result until the user checks job status. */
  if(task_kind==Task::game_start&&!game_job_accepted)game_job_reply_unknown=game_job_attempted;
  if(task_kind==Task::game_progress)game_progress_checked=false;
  if(task_kind==Task::access||task_kind==Task::environment)local_checked=false;
  task_state.store(4,std::memory_order_release);
 }else task_state.store(2,std::memory_order_release);
 return nullptr;
}
bool begin_task(Task kind) noexcept {
 if(task_state.load(std::memory_order_acquire)==1)return false;
 if(kind==Task::environment)
  general_audit_stage.store(0u,std::memory_order_release);
 task_kind=kind;
 scan_cancel_requested.store(false,std::memory_order_release);
 task_state.store(1,std::memory_order_release);
 pthread_t worker{};
 const int rc=pthread_create(&worker,nullptr,background_task,nullptr);
 if(rc!=0){
  if(kind==Task::environment)
   general_audit_stage.store(0u,std::memory_order_release);
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
  companion_summary=Cmp5CompanionReport{};
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
   if(screen==Screen::compression&&
      (compression_view==4u||compression_view==10u)&&task_busy()){
    // A posted job cannot be "undone" by dismissing the title UI.
    continue;
   }
   if(!task_busy()&&screen==Screen::compression&&compression_view>=2u){
    if(compression_view==7u){
     compression_view=5u;
    }else if(compression_view>=8u&&compression_view<=10u){
     compression_view=7u;
    }else{
     compression_view=compression_view==5u?
       compression_monitor_return_view:(!game_source_is_image?1u:6u);
     game_preflight_checked=false;
    }
    continue;
   }
   if(task_busy())scan_cancel_requested.store(true,std::memory_order_release);
   else if(screen==Screen::photo){
    // Do not retain decoded RGBA buffers after closing the preview;
    // avoid touching the buffer while its worker is still running.
    cmp5_release_preview(&decoded_photo);
    photo_ready=false;
   }else if(screen==Screen::video_info){
    video_info=Cmp5VideoMetadata{};
    video_info_ready=false;
   }else if(screen==Screen::companions){
    companion_summary=Cmp5CompanionReport{};
    companion_checked=false;
   }
   screen=(screen==Screen::photo||screen==Screen::video_info||
            screen==Screen::companions||
            (screen==Screen::backup&&!backup_from_inventory))?Screen::detail:
     screen==Screen::group_backup?
       (group_from_inventory?Screen::capsules:Screen::companions):
     screen==Screen::capsules?capsule_return_screen:
     screen==Screen::rehearsal?Screen::capsules:
     (screen==Screen::backup&&backup_from_inventory)?Screen::capsules:
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
  if(screen==Screen::video_info)continue;
  if(screen==Screen::companions){
   if((pressed&triangle)&&companion_checked&&
      companion_summary.state==CMP5_COMPANIONS_MULTIPLE&&
      detail_index<media_review.count){
    group_primary=media_review.entries[detail_index];
    group_outcome={};group_step=0u;
    group_verify_only=false;group_from_inventory=false;
    screen=Screen::group_backup;
   }
   continue;
  }
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
  if(screen==Screen::capsules){
   if(pressed&l1||pressed&r1){
    private_inventory_groups=!private_inventory_groups;
    private_inventory_selection=0u;
    continue;
   }
   const unsigned options=private_inventory_groups?
     private_inventory.group_count:private_inventory.count;
   if(private_inventory.checked&&options){
    if((pressed&r2)&&!private_inventory_groups){
     const char*id=private_inventory.ids[private_inventory_selection];
     if(cmp5_private_backup::rehearsal_capsule_id(id)){
      (void)std::snprintf(private_rehearsal_id,sizeof(private_rehearsal_id),
                          "%s",id);
      private_rehearsal_step=0u;
      private_rehearsal_outcome={};
      screen=Screen::rehearsal;
     }
     continue;
    }
    if(pressed&up)private_inventory_selection=
     (private_inventory_selection+options-1u)%options;
    if(pressed&down)private_inventory_selection=
     (private_inventory_selection+1u)%options;
    if(pressed&cross){
     if(private_inventory_groups){
      group_verify_only=true;group_from_inventory=true;
      group_outcome={};group_step=2u;
      screen=Screen::group_backup;
      (void)begin_task(Task::private_group_verify);
     }else{
      backup_verify_only=true;backup_from_inventory=true;
      backup_outcome={};backup_step=2u;
      screen=Screen::backup;
      (void)begin_task(Task::private_backup_verify);
     }
     continue;
    }
   }
   if(pressed&triangle)(void)begin_task(Task::private_inventory);
   if((pressed&square)&&private_inventory.checked)
    (void)begin_task(Task::private_archive_audit);
   continue;
  }
  if(screen==Screen::rehearsal){
   if((pressed&cross)&&private_rehearsal_step==0u)
    private_rehearsal_step=1u;
   else if((pressed&cross)&&private_rehearsal_step==1u){
    private_rehearsal_step=2u;
    (void)begin_task(Task::private_rehearsal);
   }
   continue;
  }
  if(screen==Screen::group_backup){
   if(group_verify_only)continue;
   if(group_step==0u&&(pressed&cross))group_step=1u;
   else if(group_step==1u&&(pressed&cross)){
    group_step=2u;
    (void)begin_task(Task::private_group_backup);
   }
   continue;
  }
  if(screen==Screen::backup){
   if(!backup_verify_only){
    if(backup_step==0u&&(pressed&cross))backup_step=1u;
    else if(backup_step==1u&&(pressed&cross)){
     backup_step=2u;
     (void)begin_task(Task::private_backup);
    }
   }
   continue;
  }
  if(screen==Screen::detail){
   if((pressed&l1)&&detail_index<media_review.count){
    capsule_return_screen=Screen::detail;
    private_inventory={};private_inventory_selection=0u;
    screen=Screen::capsules;
    (void)begin_task(Task::private_inventory);
    continue;
   }
   if((pressed&(r2|l2))&&detail_index<media_review.count){
    const auto&item=media_review.entries[detail_index];
    char id[32]{};
    if(cmp5_private_backup::capsule_name(item,id)){
     backup_selected=item;backup_outcome={};
     backup_verify_only=(pressed&l2)!=0u;
     backup_from_inventory=false;
     backup_step=backup_verify_only?2u:0u;
     screen=Screen::backup;
     if(backup_verify_only)
      (void)begin_task(Task::private_backup_verify);
    }
    continue;
   }
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
  if(screen==Screen::compression){
   /* Reconnect to a background job after leaving/reopening the title.
    * Monitor-only: does not implicitly claim, pause or resume that job. */
   if((compression_view==0u||compression_view==1u||
       compression_view==6u)&&(pressed&triangle)){
    compression_monitor_return_view=compression_view;
    compression_view=5u;
    game_progress_refresh.defer(compression_monotonic_ns());
    (void)begin_task(Task::game_progress);
    continue;
   }
   if(compression_view==7u){
    if(recovery_checked&&recovery_inventory.count){
     if(pressed&up)recovery_selection=
       (recovery_selection+recovery_inventory.count-1u)%recovery_inventory.count;
     if(pressed&down)recovery_selection=
       (recovery_selection+1u)%recovery_inventory.count;
    }
    if(pressed&cross)(void)begin_task(Task::recovery_scan);
    if((pressed&triangle)&&!recovery_resume_attempted&&recovery_checked&&
       recovery_selection<recovery_inventory.count){
     recovery_chosen=recovery_inventory.entries[recovery_selection];
     compression_view=8u;
    }
    continue;
   }
   if(compression_view==8u){
    if(pressed&cross)compression_view=9u;
    continue;
   }
   if(compression_view==9u){
    if(pressed&cross){
     compression_view=10u;
     (void)begin_task(Task::recovery_resume);
    }
    continue;
   }
   if(compression_view==10u){
    if(pressed&cross){
     compression_monitor_return_view=7u;compression_view=5u;
     game_progress_refresh.defer(compression_monotonic_ns());
     (void)begin_task(Task::game_progress);
    }
    continue;
   }
   if(compression_view==5u){
    if(pressed&square){
     compression_view=7u;
     (void)begin_task(Task::recovery_scan);
     continue;
    }
    if(pressed&l1||pressed&r1){
     compression_view=compression_adjacent_tab(compression_monitor_return_view,
                                              (pressed&r1)!=0u);
     continue;
    }
    if(pressed&cross){
     game_progress_refresh.defer(compression_monotonic_ns());
     (void)begin_task(Task::game_progress);
    }
    continue;
   }
   if(compression_view==0u||compression_view==1u||compression_view==6u){
    if(pressed&r1)compression_view=compression_adjacent_tab(compression_view,true);
    if(pressed&l1)compression_view=compression_adjacent_tab(compression_view,false);
   }
   if(compression_view==1u){
    if(game_images.shown_folders){
     if(pressed&up)compression_selection=
       (compression_selection+game_images.shown_folders-1u)%game_images.shown_folders;
     if(pressed&down)compression_selection=
       (compression_selection+1u)%game_images.shown_folders;
    }
    if(pressed&left)compression_destination=
      (compression_destination+game_destination_count-1u)%game_destination_count;
    if(pressed&right)compression_destination=
      (compression_destination+1u)%game_destination_count;
    if((pressed&cross)&&stage_game_preflight()){
     compression_view=2u;
     (void)begin_task(Task::game_check);
    }
   }else if(compression_view==6u){
    if(game_images.shown_images){
     if(pressed&up)compression_image_selection=
       (compression_image_selection+game_images.shown_images-1u)%game_images.shown_images;
     if(pressed&down)compression_image_selection=
       (compression_image_selection+1u)%game_images.shown_images;
    }
    if(pressed&left)compression_destination=
      (compression_destination+game_destination_count-1u)%game_destination_count;
    if(pressed&right)compression_destination=
      (compression_destination+1u)%game_destination_count;
    /* A real SMP title ID is resolved on the worker before allowing
     * confirmation, never inferred from the .exfat filename. */
    if((pressed&cross)&&stage_game_image_preflight()){
     compression_view=2u;
     (void)begin_task(Task::game_check);
    }
   }else if(compression_view==0u&&(pressed&cross)){
    (void)begin_task(Task::images);
    }else if(compression_view==2u){
    if((pressed&cross)&&game_preflight_checked&&game_preflight.ready())
     compression_view=3u;  /* first affirmative action */
    if(pressed&triangle)(void)begin_task(Task::game_check);
   }else if(compression_view==3u&&(pressed&cross)){
    compression_view=4u;  /* second affirmative action, send once */
    game_progress_refresh.defer(compression_monotonic_ns());
    (void)begin_task(Task::game_start);
   }else if(compression_view==4u){
    if(pressed&cross){
     game_progress_refresh.defer(compression_monotonic_ns());
     (void)begin_task(Task::game_progress);
    }
    if((pressed&triangle)&&game_job_accepted&&game_progress_checked&&
       std::strcmp(game_progress.title_id,game_preflight.title_id)==0){
      if(game_progress.state==cmp5_compression::JobState::running)
       (void)begin_task(Task::game_pause);
      else if(game_progress.state==cmp5_compression::JobState::paused||
              game_progress.state==cmp5_compression::JobState::error||
              game_progress.state==cmp5_compression::JobState::cancelled)
       (void)begin_task(Task::game_resume);
    }
   }
   if((compression_view==0u||compression_view==1u||compression_view==6u)&&(pressed&square))
    (void)begin_task(Task::engine_start);
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
  if(screen==Screen::apps&&(pressed&l1||pressed&r1)){
   homebrew_page^=1u;
   continue;
  }
  if(screen==Screen::analyze&&(pressed&l1||pressed&r1)){
   analyze_page^=1u;
   continue;
  }
  if(screen==Screen::cleanup&&(pressed&l1||pressed&r1)){
   cleanup_page=(pressed&r1)?(cleanup_page+1u)%4u:
     (cleanup_page+3u)%4u;
   continue;
  }
  if(screen==Screen::access&&(pressed&l1||pressed&r1)){
   access_page=(pressed&r1)?
     (access_page+1u)%access_page_count:
     (access_page+access_page_count-1u)%access_page_count;
   continue;
  }
  if(screen==Screen::access&&(pressed&triangle)){
   (void)begin_task(Task::helper_start);
   continue;
  }
  if(screen==Screen::access&&(pressed&square)){
   screen=Screen::apps;
   homebrew_page=0;
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
   /* Home shortcut: historic private capsules remain accessible even
    * with zero visible Gallery media or a failed Gallery scan. */
   if(pressed&square){
    capsule_return_screen=Screen::menu;
    private_inventory={};private_inventory_selection=0u;
    screen=Screen::capsules;
    (void)begin_task(Task::private_inventory);
    continue;
   }
   if(pressed&up)selected=(selected+row_count-1)%row_count;
   if(pressed&down)selected=(selected+1)%row_count;
   if(pressed&cross){
    screen=selected==0?Screen::analyze:selected==1?Screen::access:
      selected==2?Screen::cleanup:selected==3?Screen::compression:Screen::official;
    if(screen==Screen::analyze)(void)begin_task(Task::environment);
    if(screen==Screen::cleanup){
     cleanup_page=0u;
     (void)begin_task(Task::environment);
    }
    if(screen==Screen::access)(void)begin_task(Task::access);
    if(screen==Screen::compression)(void)begin_task(Task::images);
   }
  }else if(pressed&cross){
   if(screen==Screen::analyze)(void)begin_task(Task::environment);
   if(screen==Screen::cleanup)(void)begin_task(Task::environment);
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
  "ANALYSE", "ACCES AU STOCKAGE", "NETTOYAGE",
  "JEUX ET COMPRESSION", "RETOUR OFFICIEL"
 };
 static constexpr const char*detail[row_count]={
  "ETAT DU MOTEUR", "DROITS REELS", "PROTECTION ACTIVE",
  "IMAGES EXFAT ET LZ4", "CONTROLES LOCAUX"
 };
 for(int i=0;i<row_count;i++){
  const unsigned top=315+static_cast<unsigned>(i)*120;
  canvas.rectangle(95,top,1730,105,i==selected?Color::cyan:Color::panel);
  canvas.rectangle(104,top+8,1712,89,Color::background);
  canvas.text(145,top+25,items[i],5,i==selected?Color::cyan:Color::white);
  canvas.text(1000,top+34,detail[i],3,Color::white);
 }
 footer(canvas,task_busy()?
   "ANALYSE EN COURS    CERCLE POUR ANNULER":
   task_cancelled()?"ANALYSE ANNULEE   X POUR RECOMMENCER":
   "X OUVRIR   CARRE ARCHIVES PRIVEES   HAUT/BAS NAVIGUER");
}
void show_analyze(ps5::demo::Canvas&canvas) noexcept {
 if(task_busy()){
  const unsigned stage=general_audit_stage.load(std::memory_order_acquire);
  const char*detail=stage==1u?"SONDES DE DROITS ET MONTAGES":
    stage==2u?"CAPTURES, CACHES ET JOURNAUX":
    stage==3u?"APPLICATIONS, PLUGINS ET DONNEES":
    stage==4u?"DEMARRAGE AUTOMATIQUE ET CONFIGURATION":
    stage==5u?"DOSSIERS ET IMAGES DE JEUX":
    stage==6u?"RESULTATS EN COURS DE VALIDATION":
    "PREPARATION DES CONTROLES";
  char progress[112]{};
  (void)std::snprintf(progress,sizeof(progress),
   "ETAPE %u/5  %s",stage>=1u&&stage<=5u?stage:stage==6u?5u:0u,
   detail);
  panel(canvas,366,"ANALYSE GENERALE",progress,Color::cyan);
  panel(canvas,515,"PERIMETRE","MODULES INDEPENDANTS  DONNEES EN LECTURE SEULE");
  panel(canvas,664,"STATUT","RESULTATS PUBLIES UNIQUEMENT APRES FIN DU WORKER");
  footer(canvas,"CERCLE ANNULER   AUCUNE SUPPRESSION");
  return;
 }
 if(task_failed()||task_cancelled()){
  panel(canvas,366,task_cancelled()?"ANALYSE ANNULEE":"CONTROLE NON LANCE",
    task_cancelled()?"RESULTAT PARTIEL REJETE":"ECHEC DE CREATION DU WORKER",
    Color::yellow);
  footer(canvas,"X RETENTER    CERCLE RETOUR");
  return;
 }
 if(analyze_page==1u){
  const LocalCoverage scope=summarize_local_coverage(
    local_probe,local_path_count,local_checked);
  char result[132]{};
  if(summary_status==Summary::ok)
   (void)std::snprintf(result,sizeof(result),
    "%u FICHIERS  ACCES %s  CAPTURES %s",
    summary_files,!local_checked?"INCONNUS":scope.partial?"PARTIELS":"SONDES OK",
    summary_partial?"PARTIELLES":"OBSERVEES");
  else
   (void)std::snprintf(result,sizeof(result),
    "MEDIAS NON DISPONIBLES  ACCES %s",
    local_checked?"SONDES DISPONIBLES":"NON VERIFIES");
  panel(canvas,366,"COUVERTURE ET CAPTURES",result,
    summary_status!=Summary::ok||summary_partial||scope.partial?
       Color::yellow:Color::cyan);
  if(homebrew_checked)
   (void)std::snprintf(result,sizeof(result),
    "%u APPS  %u PLUGINS  %u DONNEES  %u INSTALLES%s",
    homebrew_stats.homebrew,homebrew_stats.plugins,
    homebrew_stats.app_data,homebrew_stats.installed,
    homebrew_stats.partial?"  PARTIEL":"");
  else (void)std::snprintf(result,sizeof(result),"INVENTAIRE NON DISPONIBLE");
  panel(canvas,515,"APPLICATIONS ET EXTENSIONS",result,
    !homebrew_checked||homebrew_stats.partial?Color::yellow:Color::cyan);
  if(autoload_checked)
   (void)std::snprintf(result,sizeof(result),
    "%u/%u CHEMINS PRESENTS  %u ILLISIBLES  %u TROP GRANDS",
    autoload_stats.present,autoload_stats.checked,
    autoload_stats.unreadable,autoload_stats.oversized);
  else (void)std::snprintf(result,sizeof(result),"AUTOLOAD NON VERIFIE");
  panel(canvas,664,"DEMARRAGE ET AUTOLOAD",result,
    !autoload_checked||autoload_stats.unreadable||autoload_stats.oversized?
       Color::yellow:Color::cyan);
  if(general_games.checked)
   (void)std::snprintf(result,sizeof(result),
    "%u DOSSIERS  %u IMAGES  %u RACINES INACCESSIBLES%s",
    general_games.folder_games,general_games.image_files,
    general_games.unavailable_roots,general_games.partial?"  PARTIEL":"");
  else (void)std::snprintf(result,sizeof(result),"JEUX NON VERIFIES");
  panel(canvas,813,"JEUX ET IMAGES DE COMPRESSION",result,
    !general_games.checked||general_games.partial?Color::yellow:Color::cyan);
  footer(canvas,general_audit_completed?
    "L1/R1 SYNTHESE   X RELANCER TOUT   CERCLE RETOUR":
    "L1/R1 SYNTHESE   X ANALYSE GENERALE   AUCUNE SUPPRESSION");
  return;
 }
 if(engine==Link::ok){
  char result[112]{};
  if(mount_reported)
   (void)std::snprintf(result,sizeof(result),
    "OUVERTS %u  REFUSES %u  ABSENTS %u  MONTAGES INCERTAINS %u",
    available,denied,absent,mount_unknown);
  else
   (void)std::snprintf(result,sizeof(result),
    "OUVERTS %u  REFUSES %u  ABSENTS %u  MONTAGES NON DETAILLES",
    available,denied,absent);
  panel(canvas,366,"DIAGNOSTIC DU MOTEUR",result,Color::cyan);
 }else if(summary_status==Summary::ok){
  const LocalCoverage scope=summarize_local_coverage(local_probe,local_path_count,local_checked);
  const bool limited=summary_partial||scope.partial;
  panel(canvas,366,"SCAN NATIF DU TITRE",
    limited?"COUVERTURE PARTIELLE OU NON PROUVEE":"SONDAGE DES RACINES DISPONIBLE",
    limited?Color::yellow:Color::cyan);
 }else{
  panel(canvas,366,"ANALYSE NON DISPONIBLE","DROITS OU MOTEUR LIMITES",Color::yellow);
 }
 const LocalCoverage coverage=summarize_local_coverage(
   local_probe,local_path_count,local_checked);
 char local_result[112]{};
 if(local_checked){
  (void)std::snprintf(local_result,sizeof(local_result),
    "%u/6 OUVERTS  %u REFUSES  %u ABSENTS%s",
    coverage.opened+coverage.listable,coverage.denied,coverage.missing,
    coverage.partial?"  PARTIEL":"");
 }else{
  (void)std::snprintf(local_result,sizeof(local_result),"SONDAGE NON TERMINE");
 }
 panel(canvas,515,"COUVERTURE DES 6 SONDES",local_result,
   coverage.partial?Color::yellow:Color::cyan);
 char detail[112]{};
 if(summary_status==Summary::ok){
  (void)std::snprintf(detail,sizeof(detail),"%u FICHIERS  %llu MIO NOMINAUX%s",
    summary_files,static_cast<unsigned long long>(summary_bytes/1048576ULL),
    summary_partial?"  PARTIEL":"");
  panel(canvas,664,"BILAN NATIF DU TITRE",detail,Color::cyan);
  (void)std::snprintf(detail,sizeof(detail),"CAPTURES %u  CACHES %u  LOGS %u  PH %s  VID %s",
    summary_media,summary_cache,summary_logs,
    gallery_label(summary_gallery_photo),gallery_label(summary_gallery_video));
  panel(canvas,813,"CATEGORIES VUES (TITRE)",detail);
  if(helper_summary_checked){
   char helper_text[120]{};
   (void)std::snprintf(helper_text,sizeof(helper_text),
      "HELPER: %u FICHIERS  OBSERVATIONS SEPAREES  NON SELECTIONNABLES",
      helper_summary.files);
   canvas.text(125,912,helper_text,2,Color::yellow);
  }
 }else{
  if(helper_summary_checked){
   char aggregate[120]{};
   (void)std::snprintf(aggregate,sizeof(aggregate),
     "%u FICHIERS  %llu MIO NOMINAUX  NON SELECTIONNABLES",
     helper_summary.files,
     static_cast<unsigned long long>(helper_summary.bytes/1048576ULL));
   panel(canvas,664,"AGREGATS HELPER UNIQUEMENT",aggregate,Color::yellow);
   panel(canvas,813,"TITRE NATIF","ACCES GALERIE NON VALIDE  NETTOYAGE BLOQUE",
     Color::yellow);
  }else{
   panel(canvas,664,"BILAN DU STOCKAGE",
     summary_status==Summary::unchecked?"X POUR LANCER LE SCAN":"COUVERTURE INDISPONIBLE",
     Color::yellow);
   panel(canvas,813,"PROTECTION","AUCUNE SUPPRESSION EFFECTUEE");
  }
 }
 footer(canvas,"X ANALYSE GENERALE   L1/R1 TOUS LES MODULES   CERCLE RETOUR");
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
 for(unsigned slot=0;slot<4u;slot++){
  const unsigned i=access_page*4u+slot;
  if(i>=local_path_count)break;
  const auto&probe=local_probe[i];
  const auto state=probe.status;
  char detail[100]{};
  if(state==LocalAccess::listable){
   if(probe.volume_known){
    if(probe.capacity_only)
     (void)std::snprintf(detail,sizeof(detail),
       "MONTAGE DETECTE  %llu/%llu GIO LIBRES/TOTAL",
       static_cast<unsigned long long>(probe.available_bytes/1073741824ULL),
       static_cast<unsigned long long>(probe.total_bytes/1073741824ULL));
    else
     (void)std::snprintf(detail,sizeof(detail),
       "%u VUES  %llu/%llu GIO LIBRES/TOTAL%s%s",probe.entries,
       static_cast<unsigned long long>(probe.available_bytes/1073741824ULL),
       static_cast<unsigned long long>(probe.total_bytes/1073741824ULL),
       probe.truncated?" (LIMITE)":"",probe.io_error?" (ERREUR)":"");
   }
   else if(probe.capacity_only)
    (void)std::snprintf(detail,sizeof(detail),
      probe.mount_confirmed?"MONTAGE DETECTE  CAPACITE INCONNUE":
                            "CHEMIN EXISTANT  MONTAGE NON CONFIRME");
   else
    (void)std::snprintf(detail,sizeof(detail),"%u ENTREES VUES%s%s",
      probe.entries,probe.truncated?" (LIMITE)":"",probe.io_error?" (ERREUR)":"");
  }else{
   const char*reason=state==LocalAccess::unchecked?"NON TESTE":
     state==LocalAccess::opened?"FICHIER OUVERT":
     state==LocalAccess::missing?"ABSENT":
     state==LocalAccess::denied?"PERMISSION REFUSEE":
     state==LocalAccess::enumeration_denied?"LISTE BLOQUEE":
     "ERREUR D ACCES";
   (void)std::snprintf(detail,sizeof(detail),"%s",reason);
  }
  const unsigned y=353+slot*144u;
   panel(canvas,y,local_names[i],detail,
     (state==LocalAccess::listable&&!probe.truncated&&!probe.io_error)||
      state==LocalAccess::opened?Color::cyan:Color::yellow);
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
 /* Storage pages show four complete mount rows without overlapping panels. */
 char privilege_note[160]{};
 (void)std::snprintf(privilege_note,sizeof(privilege_note),
   "TITRE UID %lu EUID %lu  HELPER SEPARE  SUPPRESSION BLOQUEE",
   static_cast<unsigned long>(getuid()),static_cast<unsigned long>(geteuid()));
 canvas.text(125,925,privilege_note,2,Color::yellow);
 const char*helper_hint=engine==Link::ok||
    helper_launch==EngineLaunch::active?
  "HELPER LECTURE SEULE ACTIF":
  helper_launch==EngineLaunch::submitted?
  "HELPER ENVOYE  X POUR VERIFIER":
  helper_launch==EngineLaunch::missing_file?
  "HELPER ABSENT DU PACKAGE":
  helper_launch==EngineLaunch::invalid_elf?
  "HELPER ELF INVALIDE":
  helper_launch==EngineLaunch::loader_unavailable?
  "CHARGEUR ELF INDISPONIBLE":
  helper_launch==EngineLaunch::send_failed?
  "ENVOI DU HELPER ECHOUE":
  "TRIANGLE LANCER HELPER LECTURE SEULE";
 canvas.text(125,904,helper_hint,2,Color::cyan);
 char access_footer[130]{};
 (void)std::snprintf(access_footer,sizeof(access_footer),
   "PAGE %u/%u  L1/R1 STOCKAGE  X ACTUALISER  CERCLE RETOUR",
   access_page+1u,access_page_count);
 footer(canvas,access_footer);
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
 if(homebrew_page==0u){
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
 }else{
  static constexpr const char*categories[4]={
   "HOMEBREW","PLUGIN","DONNEES APP","INSTALLATION"
  };
  for(unsigned i=0;i<4;i++){
   if(i>=homebrew_stats.top_count){
    panel(canvas,355+i*144,"AUCUN AUTRE ELEMENT","INVENTAIRE BORNE");
    continue;
   }
   const auto&item=homebrew_stats.top[i];
   const char*category=item.category>=1u&&item.category<=4u?
     categories[item.category-1u]:"INCONNU";
   char title[85]{},detail[115]{};
   (void)std::snprintf(title,sizeof(title),"%u  %s",i+1u,item.label);
   if(item.nominal_bytes>=1048576ULL)
    (void)std::snprintf(detail,sizeof(detail),
      "%s  %llu MIO NOMINAUX  %u FICHIERS%s",category,
      static_cast<unsigned long long>(item.nominal_bytes/1048576ULL),
      item.files,item.partial?"  PARTIEL":"");
   else if(item.nominal_bytes>=1024ULL)
    (void)std::snprintf(detail,sizeof(detail),
      "%s  %llu KIO NOMINAUX  %u FICHIERS%s",category,
      static_cast<unsigned long long>(item.nominal_bytes/1024ULL),
      item.files,item.partial?"  PARTIEL":"");
   else
    (void)std::snprintf(detail,sizeof(detail),
      "%s  %llu OCTETS  %u FICHIERS%s",category,
      static_cast<unsigned long long>(item.nominal_bytes),
      item.files,item.partial?"  PARTIEL":"");
   panel(canvas,355+i*144,title,detail,Color::cyan);
  }
 }
 const auto root_label=[](unsigned state) noexcept -> const char* {
  switch(state){
   case 1u:return "OK";
   case 2u:return "ABSENT";
   case 3u:return "REFUSE";
   case 4u:return "ERREUR";
   default:return "INCONNU";
  }
 };
 char footer_message[160]{};
 (void)std::snprintf(footer_message,sizeof(footer_message),
  "PAGE %u/2  L1/R1  X ACTUALISER  /DATA:%s  /USER/APP:%s  %s",
  homebrew_page+1u,root_label(homebrew_stats.data_root_state),
  root_label(homebrew_stats.installed_root_state),
  homebrew_stats.partial?"SCAN PARTIEL":"LECTURE SEULE");
 footer(canvas,footer_message);
}
void show_cleanup(ps5::demo::Canvas&canvas) noexcept {
 if(task_busy()){
  const unsigned stage=general_audit_stage.load(std::memory_order_acquire);
  const char*phase=stage==1u?"STOCKAGE ET PERMISSIONS":
    stage==2u?"CAPTURES ET METADONNEES":
    stage==3u?"APPLICATIONS ET PLUGINS":
    stage==4u?"ELEMENTS DE DEMARRAGE":
    stage==5u?"IMAGES ET DOSSIERS DE JEUX":"PREPARATION";
  char progress[128]{};
  (void)std::snprintf(progress,sizeof(progress),
    "ETAPE %u/5  %s",stage>=1u&&stage<=5u?stage:0u,phase);
  panel(canvas,355,"BILAN GENERAL DU STOCKAGE",progress,Color::cyan);
  panel(canvas,503,"ANALYSE NON DESTRUCTIVE",
    "RESULTATS PUBLIES APRES FIN DES CINQ MODULES");
  panel(canvas,799,"PROTECTION","LECTURE SEULE  AUCUNE SUPPRESSION",Color::yellow);
  footer(canvas,"ANALYSE EN COURS  CERCLE ANNULER ET RETOUR");
  return;
 }
 if(task_failed()||task_cancelled()){
  panel(canvas,355,task_cancelled()?"ANALYSE ANNULEE":"ANALYSE NON LANCEE",
   "AUCUN RESULTAT UTILISABLE",Color::yellow);
  footer(canvas,"X RETENTER    CERCLE RETOUR");
  return;
 }
 if(cleanup_page==3u){
  /* This is a comparison of independent METADATA scopes, NOT a sum of
   * occupied disk blocks or a plan for deleting files. Homebrew roots and
   * game-image/folder roots may overlap on /data/homebrew and USB mounts. */
  char observed[144]{};
  if(general_audit_completed&&summary_status==Summary::ok){
   (void)std::snprintf(observed,sizeof(observed),
     "%llu MIO NOMINAUX  %s",
     static_cast<unsigned long long>(summary_bytes/1048576ULL),
     summary_partial?"PERIMETRE PARTIEL":"OBSERVES");
  }else (void)std::snprintf(observed,sizeof(observed),
    "NON MESURE  AUCUNE TAILLE SUPPOSEE");
  panel(canvas,355,"CAPTURES ET METADONNEES",observed,Color::cyan);
  if(general_audit_completed&&homebrew_checked){
   (void)std::snprintf(observed,sizeof(observed),
     "%llu MIO NOMINAUX  %s",
     static_cast<unsigned long long>(homebrew_stats.nominal_bytes/1048576ULL),
     homebrew_stats.partial||
       homebrew_stats.data_root_state!=1u||
       homebrew_stats.installed_root_state!=1u?"PARTIEL":"OBSERVES");
  }else (void)std::snprintf(observed,sizeof(observed),"NON MESURE");
  panel(canvas,503,"APPLICATIONS ET PLUGINS",observed,Color::cyan);
  if(general_audit_completed&&general_games.checked){
   (void)std::snprintf(observed,sizeof(observed),
     "%llu MIO IMAGES  %u RACINES INACCESSIBLES%s",
     static_cast<unsigned long long>(
       general_games.image_nominal_bytes/1048576ULL),
     general_games.unavailable_roots,
     general_games.partial?"  PARTIEL":"");
  }else (void)std::snprintf(observed,sizeof(observed),"NON MESURE");
  panel(canvas,651,"IMAGES DE JEUX",observed,Color::cyan);
  if(general_audit_completed&&general_games.checked){
   (void)std::snprintf(observed,sizeof(observed),
     "%llu MIO DOSSIERS  %u DOSSIERS PARTIELS%s",
     static_cast<unsigned long long>(
       general_games.folder_nominal_bytes/1048576ULL),
     general_games.folder_partials,
     general_games.hardlinks_skipped?"  LIENS IGNORES":"");
  }else (void)std::snprintf(observed,sizeof(observed),"NON MESURE");
  panel(canvas,799,"DOSSIERS DE JEUX",observed,Color::cyan);
  footer(canvas,"VOLUMES NON ADDITIFS  CHEVAUCHEMENTS POSSIBLES  0 SUPPRESSION");
  return;
 }
 if(cleanup_page==2u){
  // All evidence below comes from completed, read-only module results.
  // Scanner counts NEVER attest ownership, backups or write permission.
  using cmp5_cleanup::Category;
  using cmp5_cleanup::Evidence;
  using cmp5_cleanup::assess;
  using cmp5_cleanup::reason_fr;
  const auto gallery=assess(Evidence{Category::gallery,
    general_audit_completed&&summary_status==Summary::ok,
    summary_partial||media_review.partial!=0u,summary_files,0u});
  /* Apps/plugins are inventory entries, NOT detected removable caches.
   * The scanner has not proven cache ownership or provider semantics. */
  const auto apps=assess(cmp5_cleanup::unclassified_app_inventory(
    general_audit_completed&&homebrew_checked,
    homebrew_stats.partial!=0u||
      homebrew_stats.data_root_state!=1u||
      homebrew_stats.installed_root_state!=1u,
    homebrew_stats.entries));
  const auto startup=assess(Evidence{Category::autoload,
    general_audit_completed&&autoload_checked,
    autoload_stats.unreadable!=0u||autoload_stats.oversized!=0u,
    autoload_stats.present,0u});
  const auto games=assess(Evidence{Category::game_images,
    general_audit_completed&&general_games.checked,general_games.partial,
    general_games.folder_games+general_games.image_files,0u});
  char message[148]{};
  (void)std::snprintf(message,sizeof(message),
    "%u OBSERVATIONS  %s",summary_files,reason_fr(gallery.first_blocker));
  panel(canvas,355,"CAPTURES ET REFERENCES GALERIE",message,Color::yellow);
  (void)std::snprintf(message,sizeof(message),
    "%u APPLICATIONS/PLUGINS  %s",homebrew_stats.entries,
    reason_fr(apps.first_blocker));
  panel(canvas,503,"CACHES NON IDENTIFIES (INVENTAIRE APPS)",message,
    Color::yellow);
  (void)std::snprintf(message,sizeof(message),
    "%u CHEMINS  %s",autoload_stats.present,reason_fr(startup.first_blocker));
  panel(canvas,651,"DEMARRAGE ET ELEMENTS ACTIFS",message,Color::yellow);
  (void)std::snprintf(message,sizeof(message),
    "%u DOSSIERS / %u IMAGES  %s",general_games.folder_games,
    general_games.image_files,reason_fr(games.first_blocker));
  panel(canvas,799,"JEUX / RESIDUS ET CACHES SYSTEME",message,Color::yellow);
  footer(canvas,"L1/R1 BILANS   PREUVES REQUISES   AUCUNE SUPPRESSION");
  return;
 }
 if(cleanup_page==1u){
  char result[144]{};
  if(general_audit_completed&&homebrew_checked)
   (void)std::snprintf(result,sizeof(result),
    "%u APPS  %u PLUGINS  %u DONNEES  %u INSTALLES%s",
    homebrew_stats.homebrew,homebrew_stats.plugins,
    homebrew_stats.app_data,homebrew_stats.installed,
    homebrew_stats.partial?"  PARTIEL":"");
  else (void)std::snprintf(result,sizeof(result),"INVENTAIRE NON VERIFIE");
  panel(canvas,355,"APPLICATIONS ET EXTENSIONS",result,Color::yellow);
  if(general_audit_completed&&autoload_checked)
   (void)std::snprintf(result,sizeof(result),
    "%u/%u PRESENTS  %u ILLISIBLES  %u TROP GRANDS",
    autoload_stats.present,autoload_stats.checked,
    autoload_stats.unreadable,autoload_stats.oversized);
  else (void)std::snprintf(result,sizeof(result),
    "ELEMENTS DE DEMARRAGE NON VERIFIES");
  panel(canvas,503,"DEMARRAGE ET CONFIGURATION",result,Color::yellow);
  if(general_audit_completed&&general_games.checked)
   (void)std::snprintf(result,sizeof(result),
    "%u DOSSIERS  %u IMAGES  %u RACINES INACCESSIBLES%s",
    general_games.folder_games,general_games.image_files,
    general_games.unavailable_roots,general_games.partial?"  PARTIEL":"");
  else (void)std::snprintf(result,sizeof(result),
    "JEUX ET IMAGES NON VERIFIES");
  panel(canvas,651,"JEUX ET IMAGES",result,Color::yellow);
  const LocalCoverage scope=summarize_local_coverage(
    local_probe,local_path_count,local_checked);
  (void)std::snprintf(result,sizeof(result),
    "SONDES %s  CAPTURES %s  LIBERABLE: NON ETABLI",
    !local_checked?"INCONNUES":scope.partial?"PARTIELLES":"OBSERVEES",
    summary_status!=Summary::ok?"NON DISPONIBLES":
      summary_partial?"PARTIELLES":"OBSERVEES");
  panel(canvas,799,"COUVERTURE ET ESPACE LIBERABLE",result,Color::yellow);
  footer(canvas,"L1/R1 PREUVES   X ANALYSE GENERALE   AUCUNE SUPPRESSION");
  return;
 }
 if(summary_status==Summary::ok){
  char result[120]{};
  (void)std::snprintf(result,sizeof(result),
   "%u OBSERVEES (PLUS DE 50 MIO)%s",summary_large,
   summary_partial?"  SCAN PARTIEL":"");
  panel(canvas,355,"CAPTURES VOLUMINEUSES",result,Color::cyan);
  (void)std::snprintf(result,sizeof(result),
   "%u OBSERVEES (MTIME 90 J)%s",summary_old,
   summary_partial?"  SCAN PARTIEL":"");
  panel(canvas,503,"CAPTURES ANCIENNES",result,Color::cyan);
  (void)std::snprintf(result,sizeof(result),
    "%llu MIO NOMINAUX  %u ANCIENNES/GROSSES%s",
    static_cast<unsigned long long>(summary_large_bytes/1048576ULL),
    summary_old_large,summary_partial?"  PARTIEL":"");
  panel(canvas,651,summary_partial?"PERIMETRE INCOMPLET":
    scan_source==ScanSource::helper?
    "ESPACE A EXAMINER (HELPER)":"ESPACE A EXAMINER",
    result,Color::yellow);
 }else{
  panel(canvas,355,"CAPTURES VOLUMINEUSES","BILAN NON DISPONIBLE",Color::yellow);
  panel(canvas,503,"CAPTURES ANCIENNES","BILAN NON DISPONIBLE",Color::yellow);
  panel(canvas,651,"MOTEUR",helper_summary_checked?
    "HELPER: AGREGATS UNIQUEMENT  PAS DE SELECTION DE MEDIAS":
    "DROITS INSUFFISANTS OU SCAN ECHOUE",Color::yellow);
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
   "CARRE CATALOGUE (%u NATIFS)  TRIANGLE DOUBLONS  L1/R1 AUTRES",media_review.count);
 canvas.text(125,963,native_catalog,3,Color::white);
 canvas.text(125,1005,"X ANALYSE GENERALE   CERCLE RETOUR   AUCUNE SUPPRESSION",3,Color::cyan);
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
  "L1 ARCHIVES  L2 VERIFIER  R2 SAUVEGARDE  TRIANGLE MARQUER  CERCLE":
  "L1 ARCHIVES  L2 VERIFIER  R2 SAUVEGARDE  TRIANGLE MARQUER  CERCLE");
}
/* A private archive audit is a bounded SHA proof, never a Sony restore
 * promise. Show the result even when one tab has no archives. */
void show_archive_audit_result(ps5::demo::Canvas&canvas,unsigned y) noexcept {
 if(!private_archive_audit.checked)return;
 char message[160]{};
 (void)std::snprintf(message,sizeof(message),
  "SHA-256  %u/%u CAPSULES  %u/%u GROUPES  %s",
  private_archive_audit.valid_capsules,private_archive_audit.capsules,
  private_archive_audit.valid_groups,private_archive_audit.groups,
  private_archive_audit.partial?"COUVERTURE PARTIELLE":"PERIMETRE BORNE");
 const bool uncertain=private_archive_audit.partial||
    private_archive_audit.valid_capsules!=private_archive_audit.capsules||
    private_archive_audit.valid_groups!=private_archive_audit.groups;
 canvas.text(130,y,message,2,uncertain?Color::yellow:Color::cyan);
}
void show_capsules(ps5::demo::Canvas&canvas) noexcept {
 panel(canvas,286,private_inventory_groups?"GROUPES PRIVES":"CAPSULES PRIVEES",
   "L1/R1 BASCULER  AUCUN CHEMIN GALERIE AFFICHE",Color::cyan);
 if(task_busy()){
  if(task_kind==Task::private_archive_audit){
   const unsigned total=private_audit_progress.total.load(
       std::memory_order_acquire);
   const unsigned done=private_audit_progress.completed.load(
       std::memory_order_acquire);
   char detail[120]{};
   if(total)
    (void)std::snprintf(detail,sizeof(detail),
      "%u/%u ARCHIVES VERIFIEES  LECTURE SHA-256",done,total);
   else
    (void)std::snprintf(detail,sizeof(detail),
      "LECTURE DU CATALOGUE PRIVE  SHA-256 ENSUITE");
   panel(canvas,470,"AUDIT PRIVE EN COURS",detail,Color::cyan);
   panel(canvas,650,"SAUVEGARDES",
     "AUCUN ORIGINAL NI INDEX SONY MODIFIE",Color::yellow);
  }else panel(canvas,470,"INVENTAIRE EN COURS",
    "LECTURE DU DOSSIER PRIVE UNIQUEMENT",Color::cyan);
  footer(canvas,"CERCLE ANNULER  AUCUNE ECRITURE");
  return;
 }
 if(!private_inventory.checked){
  panel(canvas,460,task_cancelled()?"AUDIT ANNULE":
    task_kind==Task::private_archive_audit?
      "AUDIT INCOMPLET":"INVENTAIRE NON DISPONIBLE",
    task_cancelled()?"AUCUN BILAN SHA-256 PUBLIE":
      task_kind==Task::private_archive_audit?
        "CATALOGUE MODIFIE OU STOCKAGE INACCESSIBLE":
        "STOCKAGE PRIVE INACCESSIBLE OU NON PRESENT",Color::yellow);
  footer(canvas,capsule_return_screen==Screen::menu?
    "TRIANGLE RECONTROLER  CERCLE ACCUEIL":
    "TRIANGLE RECONTROLER  CERCLE DETAIL");
  return;
 }
 const unsigned items=private_inventory_groups?
   private_inventory.group_count:private_inventory.count;
 if(!items){
  panel(canvas,460,private_inventory_groups?
      "AUCUN GROUPE PUBLIE":"AUCUNE CAPSULE CONFIRMEE",
    private_inventory.partial?"DOSSIER PARTIEL OU RESIDUS A AUDITER":
      "AUCUN IDENTIFIANT DE SAUVEGARDE PUBLIE");
  show_archive_audit_result(canvas,700);
  if(private_inventory.staged){
   char stages[160]{};
   (void)std::snprintf(stages,sizeof(stages),
     "%u TEMPORAIRES  %llu MIO NOMINAUX  %u TAILLES NON VERIFIEES",
     private_inventory.staged,
     static_cast<unsigned long long>(
       private_inventory.staged_nominal_bytes/1048576ULL),
     private_inventory.staged_unverified);
   canvas.text(130,849,stages,2,Color::yellow);
   canvas.text(130,891,"RESIDUS PRIVES  AUCUNE SUPPRESSION AUTO",2,Color::yellow);
  }
  footer(canvas,"L1/R1 TYPE  CARRE AUDITER TOUT  TRIANGLE SCANNER  CERCLE");
  return;
 }
 /* Four rows leave a dedicated, nonoverlapping audit summary area.
  * Five 116px panels previously painted underneath the result/footer. */
 const unsigned page=private_inventory_selection/4u;
 const unsigned start=page*4u;
 for(unsigned i=start;i<items&&i<start+4u;i++){
  const char*name=private_inventory_groups?
    private_inventory.group_ids[i]:private_inventory.ids[i];
  const bool has_verdict=private_archive_audit.checked&&
    i<(private_inventory_groups?private_archive_audit.groups:
                                private_archive_audit.capsules);
  const bool verified=has_verdict&&(private_inventory_groups?
     private_archive_audit.group_valid[i]:private_archive_audit.capsule_valid[i]);
  char detail[128]{};
  (void)std::snprintf(detail,sizeof(detail),"%s%s",name,
    !has_verdict?"":verified?"  SHA-256 OK":
                        "  SHA NON VALIDE OU INACCESSIBLE");
  panel(canvas,405+(i-start)*112u,
    i==private_inventory_selection?"ARCHIVE SELECTIONNEE":"ARCHIVE",
    detail,has_verdict&&!verified?Color::yellow:
      i==private_inventory_selection?Color::cyan:Color::white);
 }
 if(private_archive_audit.checked)
  show_archive_audit_result(canvas,885);
 else if(private_inventory.partial)
  canvas.text(130,885,"INVENTAIRE PARTIEL / RESIDUS POSSIBLES",2,Color::yellow);
 if(private_inventory.staged){
  char stages[148]{};
  (void)std::snprintf(stages,sizeof(stages),
    "%u TEMPORAIRES : %llu MIO NOMINAUX  %u INCONNUS  PAS DE PURGE",
    private_inventory.staged,
    static_cast<unsigned long long>(
      private_inventory.staged_nominal_bytes/1048576ULL),
    private_inventory.staged_unverified);
  canvas.text(130,925,stages,2,Color::yellow);
 }
 footer(canvas,private_inventory_groups?
    "L1/R1 TYPE  X VERIFIER  CARRE AUDIT  TRIANGLE SCAN  CERCLE":
    "L1/R1 TYPE  X VERIFIER  R2 RESTITUTION PRIVEE  CARRE AUDIT  CERCLE");
}
void show_private_rehearsal(ps5::demo::Canvas&canvas) noexcept {
 panel(canvas,305,"RESTITUTION PRIVEE DE CAPSULE",
       "AUCUNE RESTAURATION VERS LA GALERIE SONY",Color::cyan);
 panel(canvas,445,"DOSSIER DE SORTIE FIXE",
       "/DOWNLOAD0/CMP5-PRIVATE-REHEARSALS",Color::cyan);
 if(private_rehearsal_step<2u){
  panel(canvas,585,private_rehearsal_step?
    "CONFIRMATION 2/2":"CONFIRMATION 1/2",
    "NOUVELLE COPIE BRUTE  512 MIO MAX  SHA-256",Color::yellow);
  panel(canvas,735,"NON DESTRUCTIF",
    "UNE COPIE EXISTANTE N EST JAMAIS ECRASEE",Color::yellow);
  footer(canvas,"X CONFIRMER  CERCLE ANNULER  AUCUNE ECRITURE GALERIE");
  return;
 }
 if(task_busy()){
  panel(canvas,585,"RECONSTRUCTION EN COURS",
    "CONTROLE SHA-256  COPIE PRIVEE SANS ECRASEMENT",Color::cyan);
  panel(canvas,735,"ORIGINAUX",
    "AUCUNE MODIFICATION DE LA GALERIE PS5",Color::yellow);
  footer(canvas,"CERCLE ANNULER  COPIE PRIVEE PARTIELLE POSSIBLE");
  return;
 }
 const bool success=!task_failed()&&!task_cancelled()&&
   private_rehearsal_outcome.status==
     cmp5_private_backup::RehearsalStatus::verified;
 char detail[132]{};
 if(success)
  (void)std::snprintf(detail,sizeof(detail),
    "%llu MIO RECONSTRUITS  SHA-256 IDENTIQUE",
    static_cast<unsigned long long>(private_rehearsal_outcome.bytes/1048576ULL));
 else
  (void)std::snprintf(detail,sizeof(detail),
    "AUCUNE RESTITUTION CERTIFIEE  VERIFIER ESPACE / COLLISION");
 panel(canvas,585,success?"COPIE BRUTE RESTITUEE":"RESTITUTION PRIVEE REFUSEE",
       detail,success?Color::cyan:Color::yellow);
 panel(canvas,735,"INDEX SONY",
       "NON VERIFIES  AUCUN MEDIA REINTEGRE",Color::yellow);
 footer(canvas,"CERCLE ARCHIVES  AUCUNE SUPPRESSION  AUCUN ECRASEMENT");
}
void show_private_backup(ps5::demo::Canvas&canvas) noexcept {
 panel(canvas,310,backup_verify_only?
   "CONTROLE SHA-256 APRES REDEMARRAGE":"SAUVEGARDE PRIVEE DE CAPTURE",
   "AUCUN FICHIER GALERIE NE SERA SUPPRIME",Color::cyan);
 panel(canvas,452,"EMPLACEMENT DE SAUVEGARDE",
   "STOCKAGE PRIVE DE L APPLICATION  /DOWNLOAD0",Color::cyan);
 if(backup_step<2u&&!backup_verify_only){
  panel(canvas,586,backup_step?"CONFIRMATION 2/2":"CONFIRMATION 1/2",
    "COPIER UN SEUL MEDIA SANS SES INDEX SONY",Color::yellow);
  panel(canvas,720,"CAPACITE ET INTEGRITE",
    "LIMITE 512 MIO  SHA-256  COPIE SANS ECRASEMENT");
  footer(canvas,"X CONFIRMER  CERCLE ANNULER  RIEN NE SERA EFFACE");
  return;
 }
 if(task_busy()){
  panel(canvas,586,backup_verify_only?"VERIFICATION EN COURS":
    "SAUVEGARDE EN COURS",
    backup_verify_only?"RELECTURE COMPLETE DES OCTETS ET DU SHA-256":
    "COPIE + VERIFICATION SHA-256 SUR UN WORKER",Color::cyan);
  footer(canvas,"CERCLE ANNULER  ORIGINAL PRESERVE");
  return;
 }
 const bool success=backup_outcome.status==cmp5_private_backup::Result::verified &&
  !task_cancelled()&&!task_failed();
 panel(canvas,586,success?
    (backup_verify_only?"CAPSULE INTACTE":"COPIE VERIFIEE"):
    (backup_verify_only?"CAPSULE ABSENTE OU ALTEREE":"SAUVEGARDE NON CONFIRMEE"),
   success?(backup_verify_only?
     "SHA-256 RECONTROLE  L INDEX SONY RESTE INCONNU":
     "SHA-256 IDENTIQUE  ORIGINAL INCHANGE"):
    task_cancelled()?"OPERATION ANNULEE  RESTE PRIVE POSSIBLE":
    backup_verify_only?
      "CAPSULE NON VERIFIABLE  AUCUNE MODIFICATION":
    backup_outcome.status==cmp5_private_backup::Result::storage_unavailable?
      "DOSSIER PRIVE INACCESSIBLE  AUCUNE SUPPRESSION":
      "SOURCE OU COPIE NON VALIDEE  AUCUNE SUPPRESSION",
   success?Color::cyan:Color::yellow);
 if(success)
  panel(canvas,735,"IDENTIFIANT DE CAPSULE PRIVEE",backup_outcome.capsule_id);
 else
  panel(canvas,735,backup_verify_only?"LECTURE SEULE":"REPRISE",
    backup_verify_only?"AUCUN FICHIER MODIFIE":
      "UNE COPIE PARTIELLE PEUT RESTER A INSPECTER");
 footer(canvas,"CERCLE RETOUR  RESTAURATION GALERIE NON AUTORISEE");
}
void show_group_backup(ps5::demo::Canvas&canvas) noexcept {
 panel(canvas,315,group_verify_only?
   "VERIFICATION DU GROUPE ARCHIVE":"COPIE PRIVEE DES FICHIERS ASSOCIES",
   "PAS UNE RESTAURATION DE LA GALERIE",Color::cyan);
 panel(canvas,456,"GROUPE OBSERVE",
   "2 A 4 FICHIERS DE MEME CAPTURE  512 MIO MAX",Color::cyan);
 if(group_step<2u&&!group_verify_only){
  panel(canvas,596,group_step?"CONFIRMATION 2/2":"CONFIRMATION 1/2",
    "COPIE REEVALUANT CHAQUE FICHIER SOURCE",Color::yellow);
  panel(canvas,742,"FICHIERS ORIGINAUX",
    "AUCUN EFFACEMENT  AUCUN INDEX SONY MODIFIE",Color::yellow);
  footer(canvas,"X CONFIRMER  CERCLE ANNULER  AUCUNE ECRITURE GALERIE");
  return;
 }
 if(task_busy()){
  panel(canvas,596,group_verify_only?
   "RELECTURE DU GROUPE ARCHIVE":"VERIFICATION ET COPIE DU GROUPE",
   group_verify_only?"SHA-256 DE CHAQUE CAPSULE ET DU MANIFESTE":
     "SHA-256  MANIFESTE PUBLIE APRES TOUS LES FICHIERS",Color::cyan);
  footer(canvas,"CERCLE ANNULER  RESIDUS PRIVES POSSIBLES");
  return;
 }
 const bool success=group_outcome.status==
   cmp5_private_backup::GroupStatus::verified &&
   !task_failed()&&!task_cancelled();
 panel(canvas,596,success?"GROUPE VISIBLE SAUVEGARDE":
    "GROUPE NON VALIDE",
   success?"SHA-256 DE TOUS LES MEMBRES ET DU MANIFESTE VERIFIE":
    "CONTROLE PARTIEL OU INDISPONIBLE  AUCUNE SUPPRESSION",
   success?Color::cyan:Color::yellow);
 if(success){
  char receipt[128]{};
  (void)std::snprintf(receipt,sizeof(receipt),"%u MEMBRES  %llu MIO",
   group_outcome.receipt.count,
   static_cast<unsigned long long>(group_outcome.receipt.bytes/1048576u));
  panel(canvas,750,"COPIE PRIVEE VERIFIEE",receipt,Color::cyan);
 }else panel(canvas,750,"ECHEC SECURISE",
   "RESIDUS PRIVES NON EFFACES  INSPECTION NECESSAIRE",Color::yellow);
 if(group_verify_only&&success){
  const auto&sources=group_outcome.sources;
  if(!sources.checked){
   panel(canvas,868,"SOURCES D ORIGINE",
     "CONTROLE INDISPONIBLE  INDEX SONY NON VALIDES",Color::yellow);
  }else{
   char detail[148]{};
   (void)std::snprintf(detail,sizeof(detail),
     "%u INCHANGES  %u ABSENTS  %u MODIFIES  %u INCONNUS",
     sources.unchanged,sources.missing,sources.changed,sources.unknown);
   panel(canvas,868,"ETAT DES ORIGINAUX  LECTURE SEULE",detail,
     (sources.missing||sources.changed||sources.unknown)?
       Color::yellow:Color::cyan);
  }
 }
 footer(canvas,group_verify_only?
   "CERCLE ARCHIVES  SOURCES ET INDEX SONY NON RESTAURES":
   "CERCLE FICHIERS ASSOCIES  INDEX SONY NON VALIDES");
}
void show_companions(ps5::demo::Canvas&canvas) noexcept {
 if(task_busy()){
  panel(canvas,360,"FICHIERS ASSOCIES","VERIFICATION EN LECTURE SEULE",Color::cyan);
  footer(canvas,"CERCLE ANNULER  AUCUNE MODIFICATION");
  return;
 }
 if(task_cancelled()||task_failed()||!companion_checked){
  const char*reason="ACCES OU IDENTITE NON VERIFIABLE";
  if(task_cancelled()||companion_summary.failure==CMP5_COMPANION_FAILURE_CANCELLED)
   reason="INSPECTION ANNULEE";
  else if(companion_summary.failure==CMP5_COMPANION_FAILURE_DIRECTORY_LIMIT)
   reason="LIMITE DE 1024 ENTREES DU DOSSIER";
  else if(companion_summary.failure==CMP5_COMPANION_FAILURE_AMBIGUOUS)
   reason="VARIANTE INCONNUE OU FICHIER SUSPECT";
  else if(companion_summary.failure==CMP5_COMPANION_FAILURE_UNSUPPORTED_DEPENDENCY)
   reason="METADONNEE GALERIE NON PRISE EN CHARGE";
  else if(companion_summary.failure==CMP5_COMPANION_FAILURE_PERMISSION_DENIED)
   reason="ACCES REFUSE A UN FICHIER DE LA GALERIE";
  else if(companion_summary.failure==CMP5_COMPANION_FAILURE_CHANGED)
   reason="FICHIER OU DOSSIER MODIFIE PENDANT LE SCAN";
  else if(companion_summary.failure==CMP5_COMPANION_FAILURE_THUMBNAIL_UNAVAILABLE)
   reason="MINIATURE HORS DOSSIER INACCESSIBLE";
  else if(companion_summary.failure==CMP5_COMPANION_FAILURE_THUMBNAIL_UNSAFE)
   reason="MINIATURE SUSPECTE OU IDENTITE INSTABLE";
  panel(canvas,360,"INSPECTION NON FIABLE",reason,Color::yellow);
  panel(canvas,510,"SECURITE","AUCUNE DECISION DE NETTOYAGE POSSIBLE",Color::yellow);
  footer(canvas,"CERCLE DETAIL  AUCUN NETTOYAGE AUTORISE");
  return;
 }
 if(companion_summary.state==CMP5_COMPANIONS_SINGLE){
  panel(canvas,360,"CAPTURE SEULE OBSERVEE",
    "AUCUN AUTRE FICHIER DE MEME NOM VISIBLE",Color::yellow);
  panel(canvas,508,"METADONNEES",
    "ABSENCE DE DAT OU AUTRE DEPENDANCE NON PROUVEE",Color::yellow);
 }else{
  char detail[100]{};
  (void)std::snprintf(detail,sizeof(detail),"%u FICHIERS  %u MEDIAS",
   companion_summary.files,companion_summary.media);
  panel(canvas,360,"GROUPE OBSERVE",detail,Color::cyan);
  (void)std::snprintf(detail,sizeof(detail),"%u FICHIER(S) DAT",companion_summary.sidecars);
  panel(canvas,508,"METADONNEES ASSOCIEES",detail,Color::cyan);
 }
 char nominal[100]{};
 (void)std::snprintf(nominal,sizeof(nominal),"%llu MIO NOMINAUX",
   static_cast<unsigned long long>(companion_summary.nominal_bytes/1048576ULL));
 panel(canvas,656,"TAILLE OBSERVEE",nominal);
 const char*thumbnail_detail=
   companion_summary.thumbnail==CMP5_THUMBNAIL_CANDIDATE_FOUND?
    "CANDIDAT JPG.JPEG OBSERVE  ASSOCIATION NON PROUVEE":
   companion_summary.thumbnail==CMP5_THUMBNAIL_KNOWN_PATH_ABSENT?
    "ABSENT AU CHEMIN CONNU  AUTRES FORMATS POSSIBLES":
   companion_summary.thumbnail==CMP5_THUMBNAIL_INACCESSIBLE?
    "CHEMIN MINIATURE INACCESSIBLE":
   companion_summary.thumbnail==CMP5_THUMBNAIL_UNSAFE?
    "MINIATURE OU CHEMIN SUSPECT  VERIFICATION BLOQUEE":
    "MINIATURE NON VERIFIEE";
 panel(canvas,804,"MINIATURE HORS DOSSIER",thumbnail_detail,Color::yellow);
 canvas.rectangle(95,950,1730,96,Color::panel);
 canvas.text(125,968,companion_summary.state==CMP5_COMPANIONS_MULTIPLE?
   "TRIANGLE COPIER GROUPE VISIBLE  CERCLE DETAIL":
   "CERCLE DETAIL  GROUPE NON SAUVEGARDABLE",3,Color::white);
 canvas.text(125,1006,"FICHIERS VISIBLES SEULEMENT - LECTURE SEULE",3,Color::cyan);
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
void show_compression(ps5::demo::Canvas&canvas) noexcept {
 if(compression_view==8u||compression_view==9u){
  const bool second=compression_view==9u;
  panel(canvas,310,second?"CONFIRMATION 2/2":"CONFIRMATION 1/2",
    "REPRENDRE UNE CONVERSION LZ4 INTERROMPUE ?",Color::yellow);
  panel(canvas,451,"JEU ENREGISTRE",recovery_chosen.title_id);
  panel(canvas,585,"STOCKAGE DE LA SORTIE",recovery_chosen.destination);
  panel(canvas,720,"CONTROLES AVANT ENVOI",
    "CHECKPOINT + TITRE + ETAT DU MOTEUR + SOURCE REVALIDES",Color::yellow);
  canvas.text(125,884,"SOURCE ORIGINALE PRESERVEE  AUCUN NETTOYAGE",2,Color::cyan);
  footer(canvas,second?
    "X DEMANDER REPRISE  CERCLE ANNULER  PAS DE RELANCE AUTOMATIQUE":
    "X CONTINUER  CERCLE ANNULER  AUCUN FICHIER MODIFIE");
  return;
 }
 if(compression_view==10u){
  panel(canvas,315,"REPRISE LZ4 APRES REDEMARRAGE",
    recovery_chosen.title_id,Color::cyan);
  if(task_busy())
   panel(canvas,483,"CONTROLES EN COURS",
      "CHECKPOINT ET MOTEUR EN COURS DE REVALIDATION");
  else if(recovery_resume_accepted)
   panel(canvas,483,"DEMANDE ACCEPTEE",
      "X MONITEUR  VERIFIER LA PROGRESSION",Color::cyan);
  else if(recovery_resume_unknown)
   panel(canvas,483,"REPONSE PERDUE",
      "L ENVOI A PU ABOUTIR  NE PAS REPETER",Color::yellow);
  else
   panel(canvas,483,"REPRISE REFUSEE",
      "CHECKPOINT OU IDENTITE NON CONFIRMEE",Color::yellow);
  panel(canvas,681,"FICHIERS D ORIGINE",
    "AUCUNE SUPPRESSION  SORTIE PARTIELLE CONSERVEE",Color::yellow);
  footer(canvas,recovery_resume_attempted?
    "X MONITEUR  CERCLE RETOUR  AUCUN RENVOI POSSIBLE":
    "X MONITEUR  CERCLE RETOUR  RETENTER APRES NOUVEAU CONTROLE");
  return;
 }
 if(compression_view==7u){
  panel(canvas,307,"ETATS AMPR PRESENTS SUR DISQUE",
    "INVENTAIRE LECTURE SEULE  REPRISE NON AUTORISEE");
  if(task_busy()){
   panel(canvas,467,"CHECKPOINTS AMPR",
     "INSPECTION DES EMPLACEMENTS DE JEUX AUTORISES",Color::cyan);
  }else if(!recovery_checked){
   panel(canvas,467,"CHECKPOINTS AMPR",
     "LECTURE NON DISPONIBLE  AUCUN RESULTAT CERTIFIE",Color::yellow);
  }else{
   char total[160]{};
   (void)std::snprintf(total,sizeof(total),
      "%u CANDIDATS  %u NON AFFICHES  %u DOSSIERS PARCOURUS",
      recovery_inventory.count,recovery_inventory.hidden,
      recovery_inventory.folders_checked);
   panel(canvas,448,"ETAT DES RECHERCHES",total,
      recovery_inventory.partial?Color::yellow:Color::cyan);
   if(recovery_inventory.count){
    if(recovery_selection>=recovery_inventory.count)recovery_selection=0u;
    const Cmp5RecoveryCandidate&candidate=
      recovery_inventory.entries[recovery_selection];
    char name[160]{};
    (void)std::snprintf(name,sizeof(name),
      "CANDIDAT %u/%u  %s",recovery_selection+1u,
      recovery_inventory.count,candidate.title_id);
    panel(canvas,575,"JEU AVEC ETAT ENREGISTRE",name,Color::cyan);
    panel(canvas,707,"STOCKAGE OBSERVE",candidate.destination);
    const char*stage=candidate.stage==CMP5_RECOVERY_PACK?
      "PACK LZ4":candidate.stage==CMP5_RECOVERY_COPY?
      "COPIE":"VERIFICATION";
    canvas.text(125,842,stage,2,Color::cyan);
   }else{
    panel(canvas,575,"AUCUN CHECKPOINT CONFIRME",
      recovery_inventory.partial?
       "INVENTAIRE PARTIEL  D AUTRES ETATS PEUVENT ETRE MASQUES":
       "AUCUN ETAT VALIDE TROUVE SUR LES VOLUMES ACCESSIBLES",
      Color::yellow);
   }
   if(recovery_inventory.partial||recovery_inventory.unavailable){
    char warning[140]{};
    if(recovery_inventory.unavailable)
     (void)std::snprintf(warning,sizeof(warning),
       "CONTROLE PARTIEL  %u STOCKAGES NON ACCESSIBLES",
       recovery_inventory.unavailable);
    else
     (void)std::snprintf(warning,sizeof(warning),
       "CONTROLE PARTIEL  CHEMIN OU CHECKPOINT NON VERIFIABLE");
    canvas.text(125,895,warning,2,Color::yellow);
   }
  }
  canvas.text(125,934,
    "EXISTENCE DU FICHIER != AUTORISATION DU JEU NI RESTAURATION",
    2,Color::yellow);
  footer(canvas,recovery_resume_attempted?
     "X RECONTROLER  REPRISE DEJA TENTEE  CERCLE MONITEUR":
     "HAUT/BAS CHOISIR  X RECONTROLER  TRIANGLE REPRISE  CERCLE MONITEUR");
  return;
 }
 if(compression_view==5u){
  panel(canvas,307,"TACHE LZ4 DU MOTEUR INTEGRE",
    "LECTURE SEULE  MEME APRES REOUVERTURE DE CLEANMYPS5");
  if(task_busy()){
   panel(canvas,450,"SERVICE LOCAL","INTERROGATION DU TRAVAIL EN COURS",Color::cyan);
  }else if(!game_progress_checked){
   panel(canvas,450,"SERVICE LOCAL",
     "MOTEUR ARRETE OU REPONSE INVALIDE  AUCUNE ACTION ENVOYEE",
     Color::yellow);
  }else{
   const char*state=game_progress.state==cmp5_compression::JobState::running?
     "EN COURS":game_progress.state==cmp5_compression::JobState::paused?
     "EN PAUSE":game_progress.state==cmp5_compression::JobState::done?
     "TERMINE  VERIFIER LE JEU":game_progress.state==cmp5_compression::JobState::error?
     "ECHEC":game_progress.state==cmp5_compression::JobState::cancelled?
     "INTERROMPU":"INACTIF";
   panel(canvas,450,"ETAT DU MOTEUR",state,Color::cyan);
   panel(canvas,578,"IDENTIFIANT DE JEU",
     game_progress.title_id[0]?game_progress.title_id:"AUCUN JEU ACTIF");
   char detail[160]{};
   (void)std::snprintf(detail,sizeof(detail),
     "%u %%  |  %llu / %llu MIO  |  FILE %u",
     game_progress.percent,
     static_cast<unsigned long long>(game_progress.bytes_done/1048576ULL),
     static_cast<unsigned long long>(game_progress.bytes_total/1048576ULL),
     game_progress.queue_len);
   panel(canvas,712,"AVANCEMENT RECU",detail,Color::cyan);
   char stage[190]{};
   (void)std::snprintf(stage,sizeof(stage),"ETAPE: %s  |  %s",
      cmp5_compression::phase_label(game_progress.phase),
      cmp5_compression::reason_label(game_progress.reason));
   canvas.text(125,815,stage,2,
     game_progress.reason==cmp5_compression::PauseReason::none?
       Color::cyan:Color::yellow);
   canvas.text(125,885,
     "UNE TACHE TERMINEE NE GARANTIT PAS LE DEMARRAGE DU JEU",
     2,Color::yellow);
  }
  footer(canvas,"AUTO 8 S  X ACTUALISER  CARRE CHECKPOINTS  CERCLE RETOUR");
  return;
 }
 if(compression_view==4u){
  panel(canvas,313,"MOTEUR LZ4 INTEGRE",
    "CONVERSION DU JEU SELECTIONNE");
  panel(canvas,447,"SOURCE PRESERVEE",game_source_checked,Color::cyan);
  panel(canvas,575,"DESTINATION",game_dest_checked);
  if(task_busy()){
   panel(canvas,707,"DEMANDE DE CONVERSION",
     "RECONTROLE DU JEU / ENVOI AU MOTEUR EN COURS",Color::yellow);
  }else if(game_job_accepted){
   char msg[120]{};
   (void)std::snprintf(msg,sizeof(msg),
       "TRAVAIL ACCEPTE  POSITION FILE: %u",game_job_position);
   panel(canvas,707,"MOTEUR A CONFIRME LA DEMANDE",msg,Color::cyan);
  }else if(game_job_reply_unknown){
   panel(canvas,707,"RESULTAT INCERTAIN",
     "VERIFIER LE MOTEUR AVANT UNE NOUVELLE DEMANDE",Color::yellow);
  }else if(game_job_plan_changed){
   panel(canvas,707,"PLAN DE COMPRESSION MODIFIE",
     "SOURCE OU VOLUME CHANGE  CERCLE PUIS RECONTROLER",Color::yellow);
  }else{
   panel(canvas,707,"AUCUN TRAVAIL CONFIRME",
     "COMPATIBILITE / ESPACE / REPONSE A REVERIFIER",Color::yellow);
  }
  if(game_progress_checked){
   char stage[190]{};
   (void)std::snprintf(stage,sizeof(stage),"%s  |  %s",
      cmp5_compression::phase_label(game_progress.phase),
      cmp5_compression::reason_label(game_progress.reason));
   canvas.text(125,835,stage,2,
     game_progress.reason==cmp5_compression::PauseReason::none?
       Color::cyan:Color::yellow);
  }
  if(game_progress_checked&&
     std::strcmp(game_progress.title_id,game_preflight.title_id)!=0){
   canvas.text(125,899,"MOTEUR OCCUPE PAR UN AUTRE JEU - PAS DE CONTROLE POSSIBLE",
     2,Color::yellow);
  }else if(game_progress_checked){
   const char*state=game_progress.state==cmp5_compression::JobState::running?
     "CONVERSION EN COURS":game_progress.state==cmp5_compression::JobState::paused?
     "CONVERSION EN PAUSE":game_progress.state==cmp5_compression::JobState::done?
     "TRAITEMENT TERMINE  VERIFIER LE JEU":game_progress.state==cmp5_compression::JobState::error?
     "ERREUR  SORTIE PARTIELLE CONSERVEE":game_progress.state==cmp5_compression::JobState::cancelled?
     "ANNULEE  SORTIE PARTIELLE CONSERVEE":"MOTEUR PRET OU EN FILE";
   char line[150]{};
   (void)std::snprintf(line,sizeof(line),
     "%s   %u %%  %llu / %llu MIO",state,game_progress.percent,
     static_cast<unsigned long long>(game_progress.bytes_done/1048576ULL),
     static_cast<unsigned long long>(game_progress.bytes_total/1048576ULL));
   canvas.text(125,899,line,2,Color::cyan);
  }else canvas.text(125,899,"X: INTERROGER LA PROGRESSION DU MOTEUR",2,Color::cyan);
  if(game_resume_attempted){
   canvas.text(125,939,game_resume_unknown?
    "REPONSE REPRISE PERDUE  NE PAS RENVOYER  VERIFIER ETAT":
    game_resume_accepted?
    "REPRISE DEMANDEE  X POUR VERIFIER SON APPLICATION":
    "REPRISE REFUSEE AVANT EXECUTION",
    2,game_resume_accepted?Color::cyan:Color::yellow);
  }else if(game_pause_attempted){
   canvas.text(125,939,game_pause_unknown?
     "REPONSE PAUSE PERDUE  NE PAS RENVOYER  VERIFIER ETAT":
     game_pause_accepted?
     "PAUSE DEMANDEE  X POUR VERIFIER SON APPLICATION":
     "PAUSE REFUSEE AVANT EXECUTION",
     2,game_pause_accepted?Color::cyan:Color::yellow);
  }
  footer(canvas,(game_progress_checked&&
    (game_progress.state==cmp5_compression::JobState::error||
     game_progress.state==cmp5_compression::JobState::cancelled))?
    "SORTIE CONSERVEE  TRIANGLE TENTER REPRISE  X VERIFIER":
    game_job_plan_changed?
    "CERCLE RETOUR  X RECONTROLER LE JEU  2 CONFIRMATIONS":
    "X PROGRESSION  TRIANGLE PAUSE / REPRISE  CERCLE RETOUR");
  return;
 }
 if(compression_view==3u){
  panel(canvas,314,"CONFIRMATION 2/2",
       "ENVOYER LE JEU AU MOTEUR AMPR LZ4 ?",Color::yellow);
  panel(canvas,460,"JEU SOURCE NON MODIFIE",game_source_checked);
  panel(canvas,590,"DESTINATION SEPAREE",game_dest_checked);
  panel(canvas,717,"VERIFICATION",
       "PACK LZ4 + CONTROLE INTEGRITE  AUCUN EFFACEMENT");
  footer(canvas,"X CONFIRMER CONVERSION  CERCLE ANNULER");
  return;
 }
 if(compression_view==2u){
  if(task_busy()){
   panel(canvas,337,"PRECONTROLE DU JEU","VERIFICATION DU MOTEUR EN COURS",Color::cyan);
   footer(canvas,"CERCLE ANNULER  SOURCE PRESERVEE");
   return;
  }
  panel(canvas,315,"JEU CHOISI",game_source_checked);
  panel(canvas,448,"DESTINATION DEMANDEE",game_dest_checked);
  char detail[180]{};
  if(!game_preflight_checked||!game_preflight_transport){
   panel(canvas,595,"REPONSE DU MOTEUR",
       "MOTEUR INACTIF OU REPONSE NON VERIFIABLE",Color::yellow);
  }else{
   (void)std::snprintf(detail,sizeof(detail),
     "COMPATIBLE: %s   ERREURS: %u   ALERTES: %u",
     game_preflight.compatible?"OUI":
       game_preflight.compatibility_pending?"A VERIFIER":"NON",
     game_preflight.errors,game_preflight.warnings);
   panel(canvas,585,"COMPATIBILITE AMPR",detail,
     game_preflight.ready()?Color::cyan:Color::yellow);
   (void)std::snprintf(detail,sizeof(detail),
     "REQUIS MAX: %llu GIO   LIBRES: %llu GIO",
     static_cast<unsigned long long>(game_preflight.need_max/1073741824ULL),
     static_cast<unsigned long long>(game_preflight.dest_free/1073741824ULL));
   panel(canvas,732,"RESERVATION DE STOCKAGE",detail,
     game_preflight.ready()?Color::cyan:Color::yellow);
   canvas.text(120,885,game_preflight.ready()?
      "PRECONTROLE FAVORABLE  X POUR CONFIRMER 1/2":
      game_preflight.compatibility_pending?
         "JEU IMAGE NON MONTE  COMPATIBILITE NON PROUVEE":
         "CONVERSION BLOQUEE: COMPATIBILITE OU ESPACE A VERIFIER",
      2,Color::yellow);
  }
  footer(canvas,"X SI COMPATIBLE  TRIANGLE REVERIFIER  CERCLE JEUX");
  return;
 }
 if(task_busy()){
  panel(canvas,345,"IMAGES DES JEUX","ANALYSE DES TAILLES EN COURS",Color::cyan);
  panel(canvas,493,"MODE","LECTURE SEULE - AUCUNE CONVERSION");
  footer(canvas,"CERCLE ANNULER  AUCUNE ECRITURE");
  return;
 }
 if(!game_images_checked||task_failed()||task_cancelled()){
  panel(canvas,345,"IMAGES DES JEUX",task_cancelled()?
   "ANALYSE ANNULEE":"SCAN NON DISPONIBLE",Color::yellow);
  panel(canvas,493,"FORMATS","EXFAT  FFPKG  FFPFS  FFPFSC");
  footer(canvas,"X RELANCER  CERCLE RETOUR  AUCUNE MODIFICATION");
  return;
 }
 char detail[150]{};
 if(compression_view==6u){
  (void)std::snprintf(detail,sizeof(detail),
     "%u IMAGES LISTEES  %u NON LISTEES",
     game_images.shown_images,game_images.hidden_images);
  panel(canvas,308,"IMAGES EXFAT / PFS / FFPKG",detail);
  if(game_images.shown_images==0u){
   panel(canvas,465,"AUCUNE IMAGE REPEREE",
     "VERIFIER LES SUPPORTS ET SHADOWMOUNTPLUS",Color::yellow);
  }else{
   const unsigned first=(compression_image_selection/4u)*4u;
   for(unsigned i=first;i<game_images.shown_images&&i<first+4u;i++){
    const Cmp5GameImage&candidate=game_images.image[i];
    const unsigned top=449u+(i-first)*112u;
    const Color highlight=i==compression_image_selection?Color::cyan:Color::white;
    const char*label=candidate.format==CMP5_IMAGE_EXFAT?"EXFAT":
      candidate.format==CMP5_IMAGE_FFPKG?"FFPKG":
      candidate.format==CMP5_IMAGE_FFPFS?"FFPFS":"FFPFSC";
    char summary[150]{};
    (void)std::snprintf(summary,sizeof(summary),
       "%s   %llu GIO NOMINAUX   IDENTITE NON VERIFIEE",
       label,static_cast<unsigned long long>(candidate.nominal_bytes/1073741824ULL));
    panel(canvas,top,candidate.label,summary,highlight);
   }
  }
  panel(canvas,897,"DESTINATION",game_destinations[compression_destination]);
  footer(canvas,"X PRECONTROLE SMP  L1/R1 ONGLETS  FFPFSC DEJA COMPRESSE");
  return;
 }
 if(compression_view==1u){
  (void)std::snprintf(detail,sizeof(detail),
     "%u REPERTOIRES TROUVES   %u NON AFFICHABLES",
     game_images.folder_games,game_images.hidden_folders);
  panel(canvas,308,"JEUX DETECTES EN DOSSIERS",detail);
  if(game_images.shown_folders==0u){
   panel(canvas,465,"AUCUN JEU LISTABLE",
       "LES JEUX IMAGES SONT RECAPITULES DANS L'ONGLET STATISTIQUES",
       Color::yellow);
  }else{
   const unsigned first=(compression_selection/4u)*4u;
   for(unsigned i=first;i<game_images.shown_folders&&i<first+4u;i++){
    const auto&candidate=game_images.folder[i];
    const unsigned top=449u+(i-first)*112u;
    const Color highlight=i==compression_selection?Color::cyan:Color::white;
    char summary[150]{};
    if(candidate.size_complete)
     (void)std::snprintf(summary,sizeof(summary),"%llu GIO NOMINAUX",
        static_cast<unsigned long long>(candidate.nominal_bytes/1073741824ULL));
    else
     (void)std::snprintf(summary,sizeof(summary),"TAILLE INCOMPLETE");
    panel(canvas,top,candidate.label,summary,highlight);
   }
  }
  panel(canvas,897,"DESTINATION",game_destinations[compression_destination]);
  footer(canvas,"X CONTROLE DU JEU  GAUCHE/DROITE DESTINATION  CARRE MOTEUR");
  return;
 }
 (void)std::snprintf(detail,sizeof(detail),
    "%u EXFAT   %u FFPKG   %u FFPFS",
    game_images.count[CMP5_IMAGE_EXFAT],
    game_images.count[CMP5_IMAGE_FFPKG],
    game_images.count[CMP5_IMAGE_FFPFS]);
 panel(canvas,325,"IMAGES OBSERVEES",detail);
 (void)std::snprintf(detail,sizeof(detail),
    "%u FFPFSC   %llu GIO NOMINAUX",
    game_images.count[CMP5_IMAGE_FFPFSC],
    static_cast<unsigned long long>(game_images.nominal_bytes/1073741824ULL));
 panel(canvas,465,"IMAGES COMPRESSEES / ESPACE OBSERVE",detail);
 (void)std::snprintf(detail,sizeof(detail),
    "%u IMAGES 20 GIO+   %llu GIO NOMINAUX",
    game_images.large_candidates,
    static_cast<unsigned long long>(game_images.large_nominal_bytes/1073741824ULL));
 panel(canvas,605,"GROS FICHIERS A EXAMINER",detail,Color::yellow);
 (void)std::snprintf(detail,sizeof(detail),
    "%u DOSSIERS JEUX  %u DE 20 GIO+  %u INCOMPLETS%s",
    game_images.folder_games,game_images.large_folders,
    game_images.folder_partials,
    game_images.partial?"  PARTIEL":"");
 panel(canvas,745,"JEUX EN DOSSIERS EXFAT",detail,Color::yellow);
 /* Counts are always bounded; partial scans must never imply that no
  * other installed games or images exist outside known visible roots. */
 canvas.rectangle(95,885,1730,58,Color::panel);
 char engine_text[140]{};
 if(compression_engine_seen){
  (void)std::snprintf(engine_text,sizeof(engine_text),
   "SERVICE AMPR COMPATIBLE: %s (IDENTITE NON VERIFIEE)",
    compression_engine_version);
 }else{
  (void)std::snprintf(engine_text,sizeof(engine_text),
   "MOTEUR INCLUS NON ACTIF (PORT LOCAL 6973)");
 }
 canvas.text(125,900,engine_text,2,Color::cyan);
 canvas.text(135,932,"MOTEUR MODIFIE BASE SUR EXFAT2AMPR  -  CREE PAR V-10",2,Color::cyan);
 const char*load_hint=engine_launch==EngineLaunch::loader_unavailable?
    "CHARGEUR DU JAILBREAK INDISPONIBLE":
    engine_launch==EngineLaunch::missing_file?
    "MOTEUR ABSENT DU PACKAGE":
    engine_launch==EngineLaunch::invalid_elf?
    "FICHIER MOTEUR INVALIDE":
    engine_launch==EngineLaunch::send_failed?
    "ENVOI DU MOTEUR ECHOUE":
    engine_launch==EngineLaunch::submitted?
    "MOTEUR ENVOYE AU CHARGEUR  X POUR VERIFIER":
    engine_launch==EngineLaunch::active?
    "MOTEUR ACTIF  AUCUNE CONVERSION EN COURS":
    "MOTEUR LZ4 INTEGRE  APPUYER CARRE POUR LE DEMARRER";
 canvas.text(125,957,load_hint,2,Color::white);
 canvas.text(125,1000,"L1/R1 JEUX  X ANALYSER  CARRE MOTEUR  CERCLE RETOUR",2,Color::cyan);
}
/* A generic LZ4 image cannot be mounted without compatible support. This
 * read-only view never deletes installed titles or claims post-conversion
 * launchability. See docs/GAME_COMPRESSION.md for validated dependencies. */
/* Read-only periodic progress refresh. The worker prevents any network I/O
 * on the VideoOut thread, and the one-worker rule bounds concurrency. */
void auto_refresh_compression_progress() noexcept {
 if(screen!=Screen::compression||
    (compression_view!=4u&&compression_view!=5u)){
  game_progress_refresh.clear();
  return;
 }
 const auto now=compression_monotonic_ns();
 if(now==0||task_busy())return;
 if(!game_progress_refresh.armed()){
  game_progress_refresh.defer(now);
  return;
 }
 if(game_progress_refresh.due(now)&&begin_task(Task::game_progress))
  game_progress_refresh.defer(now);
}
void scene(ps5::demo::Canvas&canvas) noexcept {
 poll_pad();
 auto_refresh_compression_progress();
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
    screen==Screen::backup?"SAUVEGARDE PRIVEE":
    screen==Screen::capsules?"CAPSULES SAUVEGARDEES":
    screen==Screen::group_backup?"COPIE GROUPE PRIVE":
    screen==Screen::rehearsal?"RESTITUTION PRIVEE":
   screen==Screen::photo?"APERCU PHOTO":
   screen==Screen::video_info?"INFORMATIONS VIDEO":
   screen==Screen::autoload?"AUTOLOAD ET DEMARRAGE":
   screen==Screen::companions?"FICHIERS LIES A LA CAPTURE":
   screen==Screen::compression?"JEUX ET COMPRESSION":"RETOUR AU SYSTEME OFFICIEL";
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
   case Screen::backup:show_private_backup(canvas);break;
   case Screen::capsules:show_capsules(canvas);break;
   case Screen::group_backup:show_group_backup(canvas);break;
   case Screen::rehearsal:show_private_rehearsal(canvas);break;
  case Screen::photo:show_photo(canvas);break;
  case Screen::video_info:show_video_info(canvas);break;
  case Screen::official:show_official(canvas);break;
  case Screen::autoload:show_autoload(canvas);break;
  case Screen::companions:show_companions(canvas);break;
  case Screen::compression:show_compression(canvas);break;
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
