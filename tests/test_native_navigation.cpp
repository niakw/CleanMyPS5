// Host-only navigation test for PS5-native UI, no console and no filesystem writes.
// Inject read and close errors without relying on host permissions.
#include <dirent.h>
#include <cerrno>
static bool probe_fail_readdir_eof=false,probe_fail_closedir=false;
static unsigned probe_faults=0;
static struct dirent* test_probe_readdir(DIR*dir){
 struct dirent*entry=readdir(dir);
 if(!entry&&probe_fail_readdir_eof){
  probe_fail_readdir_eof=false;
  ++probe_faults;errno=EIO;
 }
 return entry;
}
static int test_probe_closedir(DIR*dir){
 const int rc=closedir(dir);
 if(rc==0&&probe_fail_closedir){
  probe_fail_closedir=false;
  ++probe_faults;errno=EIO;return -1;
 }
 return rc;
}
#define readdir test_probe_readdir
#define closedir test_probe_closedir
#define main cmp5_native_app_entry
#include "../native/main.cpp"
#undef main
#undef readdir
#undef closedir

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <thread>
#include <unistd.h>

namespace {
std::uint32_t injected_buttons=0;
std::atomic<bool> stall_next_scan{false};
std::atomic<bool> entered_stalled_scan{false};
std::atomic<bool> fail_next_overview{false};
std::atomic<bool> corrupt_next_overview{false};
bool mock_ampr_online=false;
unsigned mock_ampr_gets=0;
unsigned mock_ampr_receive=0;
unsigned mock_ampr_post_headers=0,mock_ampr_post_bodies=0;
unsigned mock_ampr_pauses=0,mock_ampr_resumes=0;
bool mock_ampr_paused=false;
bool mock_control_resume_body=false;
bool mock_ampr_control_drop_reply=false;
bool mock_ampr_failed=false;
bool mock_ampr_foreign_running=false;
bool mock_smp_wrong_size=false;
bool mock_ampr_pending=false;
bool mock_ampr_image_check=false;
bool mock_ampr_image_bytes_inconsistent=false;
bool mock_ampr_changed_source_bytes=false;
unsigned mock_ampr_queued_work=0;
bool mock_private_capsule_valid=false;
unsigned mock_private_verify_calls=0;
unsigned mock_private_raw_pair_reads=0;
bool mock_private_raw_pair_valid=true;
bool mock_private_group_valid=false;
unsigned mock_private_group_verify_calls=0;
bool mock_private_group_source_valid=false;
unsigned mock_private_group_source_calls=0;
bool mock_rehearsal_pair_matches=true;
unsigned mock_rehearsal_pair_reads=0;
Cmp5OriginLiveState mock_private_group_source_states[CMP5_GROUP_MAX]{
 CMP5_ORIGIN_UNCHANGED,CMP5_ORIGIN_MISSING,CMP5_ORIGIN_UNKNOWN,
 CMP5_ORIGIN_UNKNOWN
};

void press(std::uint32_t mask){
 injected_buttons=mask;
 poll_pad();
 injected_buttons=0;
 poll_pad();
}
void finish_scan(){
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
 while(task_busy()&&std::chrono::steady_clock::now()<deadline)
  std::this_thread::yield();
 assert(!task_busy());
 assert(!task_failed());
}
}

extern "C" {
/* Navigation tests have no real PS5 /download0 or Gallery filesystem.
 * Real capsule copy/SHA/restore tests are separate native C host fixtures. */
int cmp5_backup_media(const char*,const Cmp5MediaCandidate*,const char*,
 const char*,Cmp5BackupReceipt*,Cmp5CancelCheck,void*){
 assert(false&&"Synthetic UI must not write a PS5 media capsule");
 return 0;
}
int cmp5_discard_rehearsed_backup(const char*,const char*,
 const char*,const char*,uint64_t*,int*,Cmp5CancelCheck,void*){
 assert(false&&"Synthetic navigation must never unlink private raw files");
 return 0;
}
int cmp5_restore_backup(const char*,const char*,const char*,const char*,
 Cmp5BackupReceipt*,Cmp5CancelCheck,void*){
 assert(false&&"Synthetic UI must not restore any private or Sony files");
 return 0;
}
int cmp5_verify_rehearsed_backup(const char*backup,const char*id,
 const char*rehearsal,const char*raw,Cmp5BackupReceipt*receipt,
 Cmp5CancelCheck,void*){
 assert(std::strcmp(backup,"/download0/cmp5-private-backups")==0);
 assert(std::strcmp(rehearsal,"/download0/cmp5-private-rehearsals")==0);
 assert(std::strcmp(id,"CAP_0123456789ABCDEF")==0);
 assert(std::strcmp(raw,"RAW_CAP_0123456789ABCDEF")==0);
 ++mock_private_raw_pair_reads;
 if(!mock_private_raw_pair_valid)return 0;
 if(receipt){
  *receipt=Cmp5BackupReceipt{};
  receipt->published=1;
  receipt->bytes=30ULL*1024*1024;
 }
 return 1;
}
int cmp5_verify_backup(const char*folder,const char*id,Cmp5BackupReceipt*r,
 Cmp5CancelCheck,void*){
 /* Simulated read-only capsule check; no host filesystem is accessed.
  * The write path remains a fatal assertion in this navigation fixture. */
 assert(std::strcmp(folder,"/download0/cmp5-private-backups")==0);
 assert(std::strncmp(id,"CAP_",4)==0&&std::strlen(id)==20u);
 ++mock_private_verify_calls;
 if(!mock_private_capsule_valid)return 0;
 if(r){*r=Cmp5BackupReceipt{};r->bytes=30ULL*1024*1024;r->published=1;}
 return 1;
}
int cmp5_plan_capture_group(const char*,const Cmp5MediaCandidate*,
 Cmp5MediaCandidate*,uint32_t*,Cmp5CancelCheck,void*){
 /* Never scan a real host Gallery or mint an apparently complete group. */
 return 0;
}
int cmp5_validate_capture_group(const Cmp5MediaCandidate*,uint32_t){
 return 0;
}
int cmp5_backup_group(const char*,const Cmp5MediaCandidate*,uint32_t,
 const char*,const char*,Cmp5GroupReceipt*,Cmp5CancelCheck,void*){
 assert(false&&"Synthetic navigation must never write group capsules");
 return 0;
}
int cmp5_verify_group(const char*folder,const char*id,
 Cmp5GroupReceipt*receipt,Cmp5CancelCheck,void*){
 /* Only a synthetic read-only group receipt; no Gallery or host writes. */
 assert(std::strcmp(folder,"/download0/cmp5-private-backups")==0);
 assert(std::strncmp(id,"GRP_",4u)==0&&std::strlen(id)==20u);
 ++mock_private_group_verify_calls;
 if(!mock_private_group_valid)return 0;
 if(receipt){
  *receipt=Cmp5GroupReceipt{};
  receipt->count=3u;
  receipt->bytes=30ULL*1024*1024;
 }
 return 1;
}
int cmp5_restore_group(const char*,const char*,const char*,
 Cmp5GroupReceipt*,Cmp5CancelCheck,void*){
 assert(false&&"Synthetic UI must not restore group members");
 return 0;
}
int cmp5_verify_restored_group(const char*path,const char*id,
 Cmp5GroupReceipt*receipt,Cmp5CancelCheck,void*){
 assert(std::strcmp(path,"/download0/cmp5-private-rehearsals")==0);
 assert(std::strcmp(id,"GRP_0123456789ABCDEF")==0);
 if(receipt){
  *receipt=Cmp5GroupReceipt{};
  receipt->count=3u;
  receipt->bytes=30ULL*1024*1024;
 }
 return 1;
}
int cmp5_inspect_backup_restore_pair(const char*backup,const char*id,
 const char*restore,const char*restore_id,Cmp5RecoveryPairReport*pair){
 assert(std::strcmp(backup,"/download0/cmp5-private-backups")==0);
 assert(std::strcmp(restore,"/download0/cmp5-private-rehearsals")==0);
 assert(std::strcmp(id,"GRP_0123456789ABCDEF")==0);
 assert(std::strcmp(restore_id,id)==0);
 ++mock_rehearsal_pair_reads;
 if(pair){*pair=Cmp5RecoveryPairReport{};
   pair->state=mock_rehearsal_pair_matches?
      CMP5_PAIR_MATCH:CMP5_PAIR_MISMATCH;}
 return 1;
}
int cmp5_inspect_group_originals(const char*root,const char*folder,
 const char*id,Cmp5OriginLiveState states[CMP5_GROUP_MAX],
 Cmp5CancelCheck cancel,void*context){
 assert(std::strcmp(root,"/")==0);
 assert(std::strcmp(folder,"/download0/cmp5-private-backups")==0);
 assert(std::strncmp(id,"GRP_",4)==0&&std::strlen(id)==20u);
 ++mock_private_group_source_calls;
 if((cancel&&cancel(context))||!mock_private_group_source_valid)return 0;
 std::memcpy(states,mock_private_group_source_states,sizeof(mock_private_group_source_states));
 return 1;
}
int sceUserServiceInitialize(const void*){return 0;}
int sceUserServiceGetInitialUser(int *id){*id=1;return 0;}
int scePadInit(void){return 0;}
int scePadOpen(int,int,int,const void*){return 1;}
int scePadRead(int,void*buffer,int){
 auto*sample=(RawPadSample*)buffer;
 std::memset(sample,0,sizeof(*sample));
 sample->buttons=injected_buttons;
 sample->connected=1;
 return 1;
}
int sceNetSocket(const char*label,int,int,int){
 if(!mock_ampr_online)return -1;
 if(std::strcmp(label,"cmp5_ampr_probe")==0)return 17;
 if(std::strcmp(label,"cmp5_ampr_game_check")==0)return 18;
 if(std::strcmp(label,"cmp5_ampr_submit")==0)return 19;
 if(std::strcmp(label,"cmp5_ampr_progress")==0)return 20;
 if(std::strcmp(label,"cmp5_ampr_job_control")==0)return 21;
 if(std::strcmp(label,"cmp5_ampr_image_resolve")==0)return 22;
 return -1;
}
int sceNetConnect(int id,const void*,std::uint32_t){return id>=17&&id<=22?0:-1;}
int sceNetSend(int id,const void*data,std::size_t size,int){
 if(id<17||id>22)return -1;
 const std::string_view request{static_cast<const char*>(data),size};
 if(id==22){
  assert(request.find("GET /api/cmp5-image?path=%2Fdata%2Fhomebrew%2FGAME.exfat HTTP/1.0")==0);
  return static_cast<int>(size);
 }
 if(id==21){
  if(request.find("POST /api/pause HTTP/1.0")==0){
   assert(request.find("Host: 127.0.0.1:6973")!=std::string_view::npos);
   assert(request.find("Content-Length: 0")==std::string_view::npos);
   assert(request.find("Content-Length: ")!=std::string_view::npos);
   mock_control_resume_body=false;
  }else if(request.find("POST /api/resume HTTP/1.0")==0){
   assert(request.find("Content-Length: ")!=std::string_view::npos);
   mock_control_resume_body=true;
  }else if(request.find("dest=%2Fmnt%2Fusb0%2Fhomebrew%2FPPSA12345")==0){
   /* Distinguish pause from resume using the preceding HTTP route,
    * never an injected compression level. Both are scoped to one game. */
   assert(request=="dest=%2Fmnt%2Fusb0%2Fhomebrew%2FPPSA12345");
   if(mock_control_resume_body){
    ++mock_ampr_resumes;mock_ampr_paused=false;
   }else{
    ++mock_ampr_pauses;mock_ampr_paused=true;
   }
   mock_control_resume_body=false;
  }else assert(false && "unexpected pause/resume request or wrong title");
  return static_cast<int>(size);
 }
 if(id==20){
  assert(request.find("GET /api/cmp5-progress HTTP/1.0")==0);
  assert(request.find("POST")==std::string_view::npos);
  return static_cast<int>(size);
 }
 if(id==19){
  if(request.find("POST /api/start HTTP/1.0")==0){
   assert(request.find("Host: 127.0.0.1:6973")!=std::string_view::npos);
   mock_ampr_post_headers++;
  }else{
   if(request.find("path=%2Fdata%2Fhomebrew%2FGAME.exfat")!=std::string_view::npos)
     {
      assert(request.find("&smp=PPSA12345")!=std::string_view::npos);
      assert(request.find("&cmp5_size=22548578304")!=std::string_view::npos);
     }
   else assert(request.find("path=%2Fdata%2Fhomebrew%2FPPSA12345")!=std::string_view::npos);
   assert(request.find("dest=%2Fmnt%2Fusb0%2Fhomebrew")!=std::string_view::npos);
   assert(request.find("overwrite=0&remove=0&force=0")!=std::string_view::npos);
   assert(request.find("verify=1&dedup=1")!=std::string_view::npos);
   mock_ampr_post_bodies++;
  }
  return static_cast<int>(size);
 }
 if(id==18){
  mock_ampr_image_check=request.find("GAME.exfat")!=std::string_view::npos;
  if(mock_ampr_image_check)
   assert(request.find("GET /api/check?path=%2Fdata%2Fhomebrew%2FGAME.exfat&dest=%2Fmnt%2Fusb0%2Fhomebrew&smp=PPSA12345 HTTP/1.0")==0);
  else assert(request.find("GET /api/check?path=%2Fdata%2Fhomebrew%2FPPSA12345&dest=%2Fmnt%2Fusb0%2Fhomebrew HTTP/1.0")==0);
  assert(request.find("POST")==std::string_view::npos);
  return static_cast<int>(size);
 }
 assert(request.find("GET /api/hello HTTP/1.0")==0);
 assert(request.find("Host: 127.0.0.1:6973")!=std::string_view::npos);
 assert(request.find("POST")==std::string_view::npos);
 ++mock_ampr_gets;
 return static_cast<int>(size);
}
int sceNetRecv(int id,void*data,std::size_t capacity,int){
 if(id<17||id>22)return -1;
 if(mock_ampr_receive++)return 0;
 if(id==22){
  char body[160]{};
  const int body_len=std::snprintf(body,sizeof(body),
   "{\"title_id\":\"PPSA12345\",\"image_type\":\"exfatfs\",\"size\":%llu}",
   mock_smp_wrong_size?12ULL*1024ULL*1024ULL*1024ULL:
   21ULL*1024ULL*1024ULL*1024ULL);
  assert(body_len>0&&static_cast<std::size_t>(body_len)<sizeof(body));
  char reply[300]{};
  const int n=std::snprintf(reply,sizeof(reply),
   "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
   body_len,body);
  assert(n>0&&static_cast<std::size_t>(n)<capacity);
  std::memcpy(data,reply,static_cast<std::size_t>(n));
  return n;
 }
 if(id==21){
  if(mock_ampr_control_drop_reply)return -1;
  static constexpr char reply[]=
    "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n"
    "Content-Length: 11\r\nConnection: close\r\n\r\n{\"ok\":true}";
  assert(sizeof(reply)-1<=capacity);
  std::memcpy(data,reply,sizeof(reply)-1);
  return sizeof(reply)-1;
 }
 if(id==20){
  char progress[320]{};
  const bool idle=mock_ampr_post_bodies==0u&&!mock_ampr_foreign_running;
  const char*state=idle?"idle":mock_ampr_failed?"error":
   mock_ampr_paused?"paused":"running";
  const char*title=idle?"":mock_ampr_foreign_running?"PPSA98765":"PPSA12345";
  const char*phase=idle?"idle":mock_ampr_failed?"error":
    mock_ampr_paused?"paused":"pack";
  const char*reason=mock_ampr_paused?"paused_dest":"none";
  const int body_size=std::snprintf(progress,sizeof(progress),
   "{\"service\":\"CleanMyPS5-AMPR\",\"state\":\"%s\","
   "\"percent\":%u,\"bytes_done\":%u,\"bytes_total\":%u,"
   "\"queue_len\":%u,\"launch_checked\":false,\"title_id\":\"%s\","
   "\"phase\":\"%s\",\"reason\":\"%s\"}",
   state,idle?0u:45u,idle?0u:450u,idle?0u:1000u,
   mock_ampr_queued_work,title,phase,reason);
  assert(body_size>0&&static_cast<std::size_t>(body_size)<sizeof(progress));
  char reply[512]{};
  int n=std::snprintf(reply,sizeof(reply),
   "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
   body_size,progress);
  assert(n>0&&static_cast<std::size_t>(n)<capacity);
  std::memcpy(data,reply,static_cast<std::size_t>(n));
  return n;
 }
 if(id==19){
  static constexpr char ack[]=R"({"ok":true,"position":0})";
  char reply[220]{};
  int n=std::snprintf(reply,sizeof(reply),
   "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
    sizeof(ack)-1,ack);
  assert(n>0&&static_cast<std::size_t>(n)<capacity);
  std::memcpy(data,reply,static_cast<std::size_t>(n));
  return n;
 }
 if(id==18){
  char json[380]{};
  const int body_length=std::snprintf(json,sizeof(json),
   "{\"title_id\":\"PPSA12345\",\"title\":\"TEST\","
   "\"source_bytes\":%llu,\"image_bytes\":%llu,\"estimate\":\"sample\","
   "\"need_typical\":1073741824,\"need_max\":2147483648,"
   "\"duration_by_level\":[1,2,3,4,5,6,7,8,9,10,11,12],"
   "\"verify_seconds\":10,\"dest_free\":4294967296,\"dest_total\":8589934592,"
   "\"compatible\":\"%s\",\"errors\":[],\"warnings\":[]}",
   mock_ampr_changed_source_bytes?4294967297ULL:4294967296ULL,
   mock_ampr_image_check?
    (mock_ampr_image_bytes_inconsistent?12ULL*1024ULL*1024ULL*1024ULL:
     21ULL*1024ULL*1024ULL*1024ULL):0ULL,
   mock_ampr_pending?"later":"yes");
  assert(body_length>0&&static_cast<std::size_t>(body_length)<sizeof(json));
  char reply[512]{};
  const int n=std::snprintf(reply,sizeof(reply),
   "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
    body_length,json);
  assert(n>0&&static_cast<std::size_t>(n)<capacity);
  std::memcpy(data,reply,static_cast<std::size_t>(n));
  return n;
 }
 static constexpr char body[]=
  R"({"paired":true,"local":true,"version":"CleanMyPS5-1.0"})";
 char reply[300]{};
 const int n=std::snprintf(reply,sizeof(reply),
  "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
  sizeof(body)-1,body);
 assert(n>0&&static_cast<std::size_t>(n)<capacity);
 std::memcpy(data,reply,static_cast<std::size_t>(n));
 return n;
}
int sceNetSetsockopt(int,int,int,const void*,std::uint32_t){return -1;}
int sceNetSocketClose(int){mock_ampr_receive=0;return 0;}

int cmp5_scan_game_images(const char*,Cmp5GameImageStats*result,
 Cmp5CancelCheck cancel,void*context){
 *result=Cmp5GameImageStats{};
 if(cancel&&cancel(context))return 0;
 result->scanned_roots=2u;
 result->count[CMP5_IMAGE_EXFAT]=2u;
 result->count[CMP5_IMAGE_FFPFSC]=1u;
 result->shown_images=2u;
 result->image[0].format=CMP5_IMAGE_EXFAT;
 result->image[0].nominal_bytes=21ULL*1024*1024*1024;
 std::strcpy(result->image[0].path,"/data/homebrew/GAME.exfat");
 std::strcpy(result->image[0].label,"GAME.exfat");
 result->image[1].format=CMP5_IMAGE_FFPFSC;
 result->image[1].nominal_bytes=12ULL*1024*1024*1024;
 std::strcpy(result->image[1].path,"/mnt/usb0/homebrew/PACK.ffpfsc");
 std::strcpy(result->image[1].label,"PACK.ffpfsc");
 result->folder_games=2u;
 result->shown_folders=2u;
 result->folder[0].size_complete=1u;
 result->folder[0].nominal_bytes=24ULL*1024*1024*1024;
 std::strcpy(result->folder[0].label,"PPSA12345");
 std::strcpy(result->folder[0].path,"/data/homebrew/PPSA12345");
 result->folder[1].size_complete=1u;
 result->folder[1].nominal_bytes=30ULL*1024*1024*1024;
 std::strcpy(result->folder[1].label,"PPSA67890");
 std::strcpy(result->folder[1].path,"/mnt/usb0/homebrew/PPSA67890");
 result->large_candidates=1u;
 result->large_nominal_bytes=24ULL*1024*1024*1024;
 result->nominal_bytes=38ULL*1024*1024*1024;
 result->folder_nominal_bytes=54ULL*1024*1024*1024;
 result->folder_partials=1u;
 result->partial=1u;
 return 1;
}
char *cmp5_overview_with_review(const char*,std::size_t*len,Cmp5MediaReview*review){
 // Synthetic fixed result: the native title can work without any helper.
 const char*response=R"({"schemaVersion":1,"mode":"metadata-only","scope":"bounded-known-roots","cleanupValidated":false,"files":2,"visited":4,"nominalBytes":40000000,"partial":true,"categories":{"media":2,"logs":0,"cache":0,"residuals":0,"other":0},"review":{"largeMedia":0,"oldMedia":2,"oldLargeMedia":0,"largeMediaNominalBytes":0,"validatedForDeletion":false}})";
 *len=std::strlen(response);
 char*out=(char*)std::malloc(*len+1);
 assert(out);
 std::memcpy(out,response,*len+1);
 std::memset(review,0,sizeof(*review));
 review->count=2;
 review->media_scanned=2;
 review->partial=1;
 std::strcpy(review->entries[0].label,"PHOTO UN");
 std::strcpy(review->entries[0].title_id,"GAME123");
 std::strcpy(review->entries[0].game_name,"Astro's Playroom");
 std::strcpy(review->entries[0].preview_path,"/user/av_contents/photo/NPXS00000/GAME123/photo.jpg");
 review->entries[0].bytes=30ULL*1024*1024;
 review->entries[0].media_type=1;
 std::strcpy(review->entries[1].label,"VIDEO DEUX");
 std::strcpy(review->entries[1].title_id,"GAME456");
 std::strcpy(review->entries[1].game_name,"Example Game");
 std::strcpy(review->entries[1].preview_path,"/user/av_contents/video/NPXS00000/GAME456/video.mp4");
 review->entries[1].bytes=10ULL*1024*1024;
 review->entries[1].media_type=2;
 return out;
}
char *cmp5_overview_with_review_cancel(const char*root,std::size_t*len,
  Cmp5MediaReview*review,Cmp5CancelCheck cancel,void*context){
 if(stall_next_scan.exchange(false)){
  entered_stalled_scan.store(true);
  // Synchronize cancellation across two real host threads.
  for(unsigned i=0;i<500000;i++){
   if(cancel(context))return nullptr;
   std::this_thread::yield();
  }
 }
 if(cancel(context))return nullptr;
 // Model scanners which populate some rows and THEN fail or report a
 // malformed summary: no previous/partial catalog may survive either case.
 if(fail_next_overview.exchange(false)){
  char*incomplete=cmp5_overview_with_review(root,len,review);
  std::free(incomplete);
  return nullptr;
 }
 char*result=cmp5_overview_with_review(root,len,review);
 if(corrupt_next_overview.exchange(false)){
  char*scope=std::strstr(result,"bounded-known-roots");
  assert(scope);
  scope[0]='X';
 }
 return result;
}
char *cmp5_duplicates_with_stats(const char*,std::size_t*len,Cmp5DuplicateStats*stats){
 // Synthetic duplicate groups only; never access the console.
 const char json[]="{\"schemaVersion\":1,\"groups\":[]}";
 *len=sizeof(json)-1;
 char*out=(char*)std::malloc(sizeof(json));
 assert(out);
 std::memcpy(out,json,sizeof(json));
 *stats=Cmp5DuplicateStats{};
 stats->exact_groups=1;
 stats->matching_files=2;
 stats->files_considered=4;
 stats->compared_bytes=2048;
 stats->duplicate_nominal_bytes=1024;
 return out;
}
char *cmp5_duplicates_with_stats_cancel(const char*root,std::size_t*len,
  Cmp5DuplicateStats*stats,Cmp5CancelCheck cancel,void*context){
 if(cancel(context))return nullptr;
 return cmp5_duplicates_with_stats(root,len,stats);
}
int cmp5_inspect_capture_companions(const char*,const Cmp5MediaCandidate*primary,
 Cmp5CompanionReport*report,Cmp5CancelCheck cancel,void*context){
 if(!report)return 0;
 *report=Cmp5CompanionReport{};
 if(!primary||cancel(context)||!primary->preview_path[0]){
  report->failure=CMP5_COMPANION_FAILURE_UNAVAILABLE;
  return 0;
 }
 if(primary->preview_path[0]=='!'){
  report->failure=CMP5_COMPANION_FAILURE_DIRECTORY_LIMIT;
  return 0;
 }
 if(primary->media_type==1u){
  report->state=CMP5_COMPANIONS_MULTIPLE;
  report->files=2u;report->media=1u;report->sidecars=1u;
  report->nominal_bytes=primary->bytes+256u;
 }else if(primary->media_type==2u){
  report->state=CMP5_COMPANIONS_SINGLE;
  report->files=1u;report->media=1u;
  report->nominal_bytes=primary->bytes;
 }else return 0;
 return 1;
}
int cmp5_probe_video_snapshot(const char*,const Cmp5MediaCandidate*c,
 Cmp5VideoMetadata*out,Cmp5CancelCheck cancelled,void*context){
 *out=Cmp5VideoMetadata{};
 if((cancelled&&cancelled(context))||c->media_type!=2u)return 0;
 out->duration_ms=95200;out->width=1920;out->height=1080;
 out->boxes_visited=6;
 return 1;
}
uint8_t *cmp5_read_photo_snapshot(const char*,const Cmp5MediaCandidate*c,size_t*size){
 if(!c||!c->preview_path[0])return nullptr;
 uint8_t*payload=(uint8_t*)std::malloc(8);
 assert(payload);
 *size=8;
 return payload;
}
int cmp5_decode_preview(const uint8_t*,std::size_t,Cmp5PreviewImage*out){
 out->width=32;out->height=16;
 out->pixels=(uint32_t*)std::malloc(32u*16u*sizeof(uint32_t));
 assert(out->pixels);
 for(unsigned i=0;i<32u*16u;i++)out->pixels[i]=0xff333333u;
 return 1;
}
void cmp5_release_preview(Cmp5PreviewImage*out){
 std::free(out->pixels);
 *out=Cmp5PreviewImage{};
}
char *cmp5_homebrew_with_stats(const char*,std::size_t*len,
  Cmp5HomebrewStats*stats,Cmp5CancelCheck cancel,void*context){
 if(cancel(context))return nullptr;
 const char text[]="{\"schemaVersion\":1,\"mode\":\"read-only\"}";
 char*result=(char*)std::malloc(sizeof(text));
 assert(result);
 std::memcpy(result,text,sizeof(text));
 *len=sizeof(text)-1;
 *stats=Cmp5HomebrewStats{};
 stats->entries=8;stats->homebrew=3;stats->plugins=2;
 stats->app_data=1;stats->installed=2;stats->partial=1;
 stats->nominal_bytes=400000000;
 return result;
}
char *cmp5_diagnostics_with_stats_cancel(const char*,std::size_t*len,
 Cmp5AutoloadStats*stats,Cmp5CancelCheck cancel,void*context){
 if(cancel&&cancel(context))return nullptr;
 const char fixture[]="{\"checked\":11,\"entries\":[]}";
 char*response=(char*)std::malloc(sizeof(fixture));
 assert(response);
 std::memcpy(response,fixture,sizeof(fixture));
 *len=sizeof(fixture)-1;
 *stats=Cmp5AutoloadStats{};
 stats->checked=11;
 stats->present=5;
 stats->missing=6;
 stats->etahen_references=1;
 stats->other_references=1;
 stats->autostart_enabled=1;
 stats->autostart_disabled=1;
 stats->unreadable=1;
 return response;
}
char *cmp5_diagnostics_with_stats(const char*root,std::size_t*len,
 Cmp5AutoloadStats*stats){
 return cmp5_diagnostics_with_stats_cancel(root,len,stats,nullptr,nullptr);
}
}

namespace ps5::demo {
[[noreturn]] void run(DrawScene,std::string_view) noexcept {std::abort();}
}

int main(){
 // The native sampler must never follow filesystem aliases or mistake a
 // missing directory for an empty one. This fixture is host-only.
 char folder[]="/tmp/cmp5-probe-XXXXXX";
 char*dir=mkdtemp(folder);
 assert(dir);
 char media[256]{},alias[256]{},file_alias[256]{},missing[256]{};
 (void)std::snprintf(media,sizeof(media),"%s/fixture.txt",dir);
 (void)std::snprintf(alias,sizeof(alias),"%s-link",dir);
 (void)std::snprintf(file_alias,sizeof(file_alias),"%s-file-link",dir);
 (void)std::snprintf(missing,sizeof(missing),"%s-missing",dir);
 FILE*fixture=std::fopen(media,"wb");assert(fixture);
 assert(std::fputs("synthetic fixture",fixture)>=0);
 assert(std::fclose(fixture)==0);
 LocalProbe sample=probe_path(dir,false);
 assert(sample.status==LocalAccess::listable);
 assert(sample.entries==1&&!sample.truncated&&!sample.io_error);
 assert(sample.volume_known&&sample.total_bytes>=sample.available_bytes);
 /* A present directory is not evidence of a USB device. Without an
  * actual mount, fstatvfs would return its parent's free-space numbers. */
 char bare_mount[256]{};
 assert(std::snprintf(bare_mount,sizeof(bare_mount),
   "%s/usb-unmounted",dir)>0);
 assert(mkdir(bare_mount,0700)==0);
 const LocalProbe absent_mount=probe_path(bare_mount,false,true);
 assert(absent_mount.status==LocalAccess::listable&&
   absent_mount.capacity_only&&!absent_mount.mount_confirmed&&
   !absent_mount.volume_known&&absent_mount.total_bytes==0u&&
   absent_mount.available_bytes==0u);
 assert(rmdir(bare_mount)==0);
 /* Visible USB screen must not imply an attached disk or show free GB. */
 local_probe[6]=absent_mount;
 access_page=1u;
 ps5::demo::Canvas usb_screen{};
 show_access(usb_screen);
 bool usb_unconfirmed=false;
 for(const auto&draw:usb_screen.commands)
  if(draw.text.find("MONTAGE NON CONFIRME")!=std::string::npos)
   usb_unconfirmed=true;
 assert(usb_unconfirmed);
 access_page=0u;
 local_probe[6]=LocalProbe{};
 struct stat child_stat{},parent_stat{};
 child_stat.st_mode=parent_stat.st_mode=S_IFDIR|0700;
 child_stat.st_dev=15;parent_stat.st_dev=15;
 assert(!mount_device_differs(child_stat,parent_stat));
 child_stat.st_dev=16;
 assert(mount_device_differs(child_stat,parent_stat));
 child_stat.st_mode=S_IFREG|0600;
 assert(!mount_device_differs(child_stat,parent_stat));
 probe_fail_readdir_eof=true;
 LocalProbe bad_read=probe_path(dir,false);
 assert(probe_faults==1u&&bad_read.status==LocalAccess::listable);
 assert(bad_read.io_error&&!bad_read.truncated&&bad_read.entries==sample.entries);
 probe_fail_closedir=true;
 LocalProbe bad_close=probe_path(dir,false);
 assert(probe_faults==2u&&bad_close.status==LocalAccess::listable);
 assert(bad_close.io_error&&!bad_close.truncated&&bad_close.entries==sample.entries);
 LocalProbe io_roots[local_path_count]{};
 for(std::size_t i=0;i<core_access_count;i++)
  io_roots[i].status=LocalAccess::listable;
 io_roots[0].status=LocalAccess::opened;
 io_roots[4]=bad_read;
 assert(summarize_local_coverage(io_roots,local_path_count,true).partial);
 io_roots[4]=bad_close;
 assert(summarize_local_coverage(io_roots,local_path_count,true).partial);
 assert(!probe_fail_closedir&&!probe_fail_readdir_eof);
 assert(probe_path(media,true).status==LocalAccess::opened);
 // Even if the path opens, a directory must never count as the title's
 // expected regular-file metadata probe.
 assert(probe_path(dir,true).status==LocalAccess::unavailable);
 assert(probe_path(missing,false).status==LocalAccess::missing);
 assert(symlink(dir,alias)==0);
 sample=probe_path(alias,false);
 assert(sample.status!=LocalAccess::listable&&!sample.volume_known);
 assert(symlink(media,file_alias)==0);
 assert(probe_path(file_alias,true).status!=LocalAccess::opened);
 /* O_NOFOLLOW only on the leaf is insufficient: a parent alias must NOT
  * redirect either directory enumeration or a metadata-file open. */
 char real_parent[256]{},alias_parent[256]{},through_alias[256]{};
 char through_alias_file[256]{},direct_child[256]{};
 (void)std::snprintf(real_parent,sizeof(real_parent),"%s/real",dir);
 (void)std::snprintf(alias_parent,sizeof(alias_parent),"%s/alias-parent",dir);
 (void)std::snprintf(through_alias,sizeof(through_alias),"%s/alias-parent",dir);
 (void)std::snprintf(through_alias_file,sizeof(through_alias_file),
    "%s/alias-parent/item.txt",dir);
 (void)std::snprintf(direct_child,sizeof(direct_child),"%s/real/item.txt",dir);
 assert(mkdir(real_parent,0700)==0);
 FILE*child=std::fopen(direct_child,"wb");assert(child);
 assert(std::fputs("TEST-ONLY",child)>=0&&std::fclose(child)==0);
 assert(symlink(real_parent,alias_parent)==0);
 assert(probe_path(real_parent,false).status==LocalAccess::listable);
 assert(probe_path(direct_child,true).status==LocalAccess::opened);
 assert(probe_path(through_alias,false).status!=LocalAccess::listable);
 assert(probe_path(through_alias_file,true).status!=LocalAccess::opened);
 assert(probe_path("/tmp/../tmp",false).status!=LocalAccess::listable);
 assert(probe_path("/tmp//untrusted",false).status!=LocalAccess::listable);
 assert(unlink(alias_parent)==0);
 assert(unlink(direct_child)==0&&rmdir(real_parent)==0);
 assert(unlink(file_alias)==0&&unlink(alias)==0);
 assert(unlink(media)==0&&rmdir(dir)==0);
 /* Six successful probes do NOT mean a full PS5 disk inventory. A
  * permission error, absent mount, truncated directory or read error
  * must make the UI coverage visibly partial, even with a green scan. */
 LocalProbe roots[local_path_count]{};
 roots[0].status=LocalAccess::opened;
 roots[1].status=LocalAccess::listable;roots[1].entries=3;
 roots[2].status=LocalAccess::listable;roots[2].entries=2;
 roots[3].status=LocalAccess::listable;roots[3].entries=5;
 roots[4].status=LocalAccess::listable;
 roots[5].status=LocalAccess::listable;
 LocalCoverage coverage=summarize_local_coverage(roots,local_path_count,true);
 assert(!coverage.partial&&coverage.opened==1u&&coverage.listable==5u);
 /* An optional missing USB port must not invalidate gallery coverage. */
 roots[6].status=LocalAccess::missing;
 coverage=summarize_local_coverage(roots,local_path_count,true);
 assert(!coverage.partial&&coverage.missing==0u);
 roots[6]=LocalProbe{};
 assert(coverage.entries==10u&&!coverage.missing&&!coverage.denied);
 assert(summarize_local_coverage(roots,local_path_count,false).partial);
 assert(summarize_local_coverage(roots,3u,true).partial);
 roots[3].status=LocalAccess::denied;
 coverage=summarize_local_coverage(roots,local_path_count,true);
 assert(coverage.partial&&coverage.denied==1u&&coverage.listable==4u);
 roots[3].status=LocalAccess::missing;
 coverage=summarize_local_coverage(roots,local_path_count,true);
 assert(coverage.partial&&coverage.missing==1u);
 roots[3].status=LocalAccess::enumeration_denied;
 coverage=summarize_local_coverage(roots,local_path_count,true);
 assert(coverage.partial&&coverage.denied==1u);
 roots[3].status=LocalAccess::unavailable;
 coverage=summarize_local_coverage(roots,local_path_count,true);
 assert(coverage.partial&&coverage.other==1u);
 roots[3].status=LocalAccess::listable;roots[3].entries=5;
 roots[2].truncated=true;
 coverage=summarize_local_coverage(roots,local_path_count,true);
 assert(coverage.partial&&coverage.truncated&&coverage.entries==10u);
 roots[2].truncated=false;roots[2].io_error=true;
 coverage=summarize_local_coverage(roots,local_path_count,true);
 assert(coverage.partial&&coverage.io_error);
 roots[2].io_error=false;
 /* A listable /user/av_contents parent is not permission to read
  * either child gallery directory. Keep the whole audit partial. */
 roots[4].status=LocalAccess::denied;
 coverage=summarize_local_coverage(roots,local_path_count,true);
 assert(roots[3].status==LocalAccess::listable&&coverage.denied==1u);
 assert(coverage.partial);
 roots[4].status=LocalAccess::listable;
 roots[5].status=LocalAccess::denied;
 coverage=summarize_local_coverage(roots,local_path_count,true);
 assert(coverage.denied==1u&&coverage.partial);
 roots[5].status=LocalAccess::listable;
 assert(!summarize_local_coverage(roots,local_path_count,true).partial);
 controller=1;
 ps5::demo::Canvas canvas;
 assert(screen==Screen::menu);
 /* Archive is reachable on FIRST launch without scanning Gallery.
  * A simulated archived capsule may be checked by ID even with zero
  * visible media. The UI fixture never touches PS5 paths or writes. */
 assert(media_review.count==0u);
 press(square);assert(screen==Screen::capsules);
 finish_scan();
 assert(!private_inventory.checked);
 private_inventory=cmp5_private_backup::Inventory{};
 private_inventory.checked=true;private_inventory.count=1u;
 std::strcpy(private_inventory.ids[0],"CAP_0123456789ABCDEF");
 private_inventory_selection=0u;
 const unsigned archive_checks_before=mock_private_verify_calls;
 mock_private_capsule_valid=true;
 press(cross);assert(screen==Screen::backup&&backup_from_inventory);
 finish_scan();
 assert(mock_private_verify_calls==archive_checks_before+1u);
 assert(backup_outcome.status==cmp5_private_backup::Result::verified);
 press(circle);assert(screen==Screen::capsules);
 /* Read-only single-archive audit after restart, no write-side mock. */
 const unsigned old_pair_reads=mock_private_raw_pair_reads;
 press(l2);
 assert(screen==Screen::rehearsal&&!private_rehearsal_groups&&
        private_rehearsal_verify_only);
 finish_scan();
 assert(private_rehearsal_outcome.status==
   cmp5_private_backup::RehearsalStatus::verified);
 assert(mock_private_raw_pair_reads==old_pair_reads+1u);
 show_private_rehearsal(canvas);
 press(circle);assert(screen==Screen::capsules);
 mock_private_raw_pair_valid=false;
 press(l2);finish_scan();
 assert(private_rehearsal_outcome.status==
   cmp5_private_backup::RehearsalStatus::failed);
 press(circle);assert(screen==Screen::capsules);
 mock_private_raw_pair_valid=true;
 /* R3 is the only path to app-private raw removal; it requires two
  * explicit presses and refuses an invalidated snapshot before any C
  * deletion entrypoint can be reached. The real writer is never called. */
 press(r3);
 assert(screen==Screen::rehearsal&&private_rehearsal_discard&&
        !private_rehearsal_groups&&private_rehearsal_step==0u);
 press(cross);
 assert(private_rehearsal_step==1u&&!task_busy());
 show_private_rehearsal(canvas);
 private_rehearsal_id[0]=0;
 press(cross);finish_scan();
 assert(private_rehearsal_outcome.status==
   cmp5_private_backup::RehearsalStatus::denied);
 press(circle);
 assert(screen==Screen::capsules);
 /* New private reconstruction workflow: R2 on a capsule, two presses,
  * no media or filesystem write before confirmation. Invalidated
  * selection must fail closed before any attempted destination creation. */
 assert(cmp5_private_backup::rehearsal_capsule_id(
   "CAP_0123456789ABCDEF"));
 assert(!cmp5_private_backup::rehearsal_capsule_id(
   "CAP_0123456789ABCDEG"));
 const unsigned prior_rehearsal_checks=mock_private_verify_calls;
 press(r2);
 assert(screen==Screen::rehearsal&&private_rehearsal_step==0u);
 assert(std::strcmp(private_rehearsal_id,"CAP_0123456789ABCDEF")==0);
 press(cross);assert(private_rehearsal_step==1u&&!task_busy());
 assert(mock_private_verify_calls==prior_rehearsal_checks);
 show_private_rehearsal(canvas);
 /* Simulate an invalid selection changed before final confirmation. */
 private_rehearsal_id[0]=0;
 press(cross);assert(private_rehearsal_step==2u);
 finish_scan();
 assert(private_rehearsal_outcome.status==
   cmp5_private_backup::RehearsalStatus::denied);
 assert(mock_private_verify_calls==prior_rehearsal_checks);
 press(circle);assert(screen==Screen::capsules);
 private_inventory.group_count=1u;
 std::strcpy(private_inventory.group_ids[0],"GRP_0123456789ABCDEF");
 mock_private_group_valid=true;
 mock_private_group_source_valid=true;
 const unsigned group_reads_before=mock_private_group_verify_calls;
 const unsigned source_reads_before=mock_private_group_source_calls;
 press(r1);assert(private_inventory_groups);
 /* Group uses the same two confirmations but a distinct group ID, and
  * cannot be retargeted into a different source by another UI event. */
 press(r2);
 assert(screen==Screen::rehearsal&&private_rehearsal_groups);
 assert(private_rehearsal_step==0u);
 assert(std::strcmp(private_rehearsal_id,"GRP_0123456789ABCDEF")==0);
 show_private_rehearsal(canvas);
 press(cross);assert(private_rehearsal_step==1u);
 private_rehearsal_id[0]=0;
 press(cross);finish_scan();
 assert(private_rehearsal_outcome.status==
   cmp5_private_backup::RehearsalStatus::denied);
 press(circle);assert(screen==Screen::capsules);
 assert(private_inventory_groups);
 press(cross);assert(screen==Screen::group_backup&&group_verify_only);
 finish_scan();
 assert(mock_private_group_verify_calls==group_reads_before+1u);
 assert(mock_private_group_source_calls==source_reads_before+1u);
 assert(group_outcome.status==cmp5_private_backup::GroupStatus::verified);
 assert(group_outcome.receipt.count==3u);
 assert(group_outcome.sources.checked);
 assert(group_outcome.sources.unchanged==1u);
 assert(group_outcome.sources.missing==1u);
 assert(group_outcome.sources.changed==0u);
 assert(group_outcome.sources.unknown==1u);
 show_group_backup(canvas); // Render status without triggering Gallery writes.
 /* A failed source inspection does not erase a valid, independently
  * rehashed archive: report UNKNOWN rather than claim restoration. */
 mock_private_group_source_valid=false;
 const auto unavailable=cmp5_private_backup::verify_saved_group(
  "GRP_0123456789ABCDEF",nullptr,nullptr);
 assert(unavailable.status==cmp5_private_backup::GroupStatus::verified);
 assert(!unavailable.sources.checked);
 assert(unavailable.sources.unchanged==0u&&unavailable.sources.missing==0u);
 mock_private_group_valid=false;
 const auto corrupt=cmp5_private_backup::verify_saved_group(
  "GRP_0123456789ABCDEF",nullptr,nullptr);
 assert(corrupt.status==cmp5_private_backup::GroupStatus::failed);
 assert(!corrupt.sources.checked);
 mock_private_group_valid=true;
 press(circle);assert(screen==Screen::capsules);
 /* Reopen a private group pair after restart using L2. This is strictly
  * read-only and must identify a changed or incomplete rehearsal. */
 const unsigned pair_before=mock_rehearsal_pair_reads;
 press(l2);assert(screen==Screen::rehearsal&&
                   private_rehearsal_groups&&private_rehearsal_verify_only);
 finish_scan();
 assert(mock_rehearsal_pair_reads==pair_before+1u);
 assert(private_rehearsal_outcome.status==
   cmp5_private_backup::RehearsalStatus::verified);
 show_private_rehearsal(canvas);
 press(circle);assert(screen==Screen::capsules);
 mock_rehearsal_pair_matches=false;
 press(l2);finish_scan();
 assert(private_rehearsal_outcome.status==
   cmp5_private_backup::RehearsalStatus::failed);
 press(circle);assert(screen==Screen::capsules);
 mock_rehearsal_pair_matches=true;
 /* Verify whole listed sets without falsely inferring Gallery restoration
  * or requiring the present existence of the source media. */
 cmp5_private_backup::ArchiveAudit audit_result{};
 cmp5_private_backup::ArchiveAuditProgress audit_progress{};
 const unsigned capsule_reads=mock_private_verify_calls;
 const unsigned group_reads=mock_private_group_verify_calls;
 const unsigned source_reads=mock_private_group_source_calls;
 assert(cmp5_private_backup::audit_inventory_at(private_inventory,
   cmp5_private_backup::workspace,&audit_result,nullptr,nullptr,&audit_progress));
 assert(audit_progress.total.load()==2u&&audit_progress.completed.load()==2u);
 assert(audit_result.checked&&!audit_result.partial&&audit_result.capsules==1u&&
   audit_result.groups==1u&&audit_result.valid_capsules==1u&&
   audit_result.valid_groups==1u&&audit_result.verified_bytes==60ULL*1024*1024);
 assert(mock_private_verify_calls==capsule_reads+1u);
 assert(mock_private_group_verify_calls==group_reads+1u);
 assert(mock_private_group_source_calls==source_reads);
 private_archive_audit=audit_result;
 show_capsules(canvas);
 mock_private_capsule_valid=false;
 mock_private_group_valid=false;
 assert(cmp5_private_backup::audit_inventory_at(private_inventory,
   cmp5_private_backup::workspace,&audit_result,nullptr,nullptr,&audit_progress));
 assert(audit_progress.total.load()==2u&&audit_progress.completed.load()==2u);
 assert(audit_result.checked&&audit_result.capsules==1u&&
   audit_result.groups==1u&&!audit_result.valid_capsules&&
   !audit_result.valid_groups&&!audit_result.verified_bytes);
 const auto immediately_cancelled=[](void*)->int{return 1;};
 audit_result.checked=true;audit_result.valid_capsules=999u;
 assert(!cmp5_private_backup::audit_inventory_at(private_inventory,
   cmp5_private_backup::workspace,&audit_result,immediately_cancelled,nullptr,
   &audit_progress));
 assert(!audit_result.checked&&!audit_result.valid_capsules);
 assert(audit_progress.total.load()==0u&&audit_progress.completed.load()==0u);
 struct CancelAfterOne {unsigned calls=0u;} stop_after_one{};
 const auto cancel_at_second=[](void*ctx)->int{
  auto*state=static_cast<CancelAfterOne*>(ctx);
  return ++state->calls>=3u?1:0;
 };
 audit_result.checked=true;
 assert(!cmp5_private_backup::audit_inventory_at(private_inventory,
   cmp5_private_backup::workspace,&audit_result,cancel_at_second,
   &stop_after_one,&audit_progress));
 assert(!audit_result.checked);
 assert(audit_progress.total.load()==2u&&audit_progress.completed.load()==1u);
 mock_private_capsule_valid=true;
 mock_private_group_valid=true;
 /* An empty tab must STILL render the independently completed bulk
  * audit report, rather than return early with only "no archive". */
 private_inventory=cmp5_private_backup::Inventory{};
 private_inventory.checked=true;
 private_archive_audit=cmp5_private_backup::ArchiveAudit{};
 private_archive_audit.checked=true;
 private_archive_audit.capsules=1u;
 private_archive_audit.valid_capsules=1u;
 private_archive_audit.groups=0u;
 canvas.clear(Color::background);
 show_capsules(canvas);
 bool found_empty_summary=false;
 for(const auto&command:canvas.commands)
  if(command.kind=='X'&&command.text.find("SHA-256  1/1 CAPSULES  0/0 GROUPES")!=
      std::string::npos)found_empty_summary=true;
 assert(found_empty_summary);
 /* Four visible rows + a fixed summary area; the fifth item paginates.
  * There must be no private-archive panel drawn over the audit footer. */
 private_inventory_groups=false; // Show the populated capsule tab.
 private_inventory.count=5u;
 for(unsigned i=0;i<private_inventory.count;i++)
  (void)std::snprintf(private_inventory.ids[i],
    sizeof(private_inventory.ids[i]),"CAP_%016X",i+1u);
 private_archive_audit.capsules=5u;
 private_archive_audit.valid_capsules=4u;
 for(unsigned i=0;i<4u;i++)private_archive_audit.capsule_valid[i]=true;
 private_archive_audit.capsule_valid[4]=false;
 canvas.clear(Color::background);
 show_capsules(canvas);
 unsigned visible_rows=0u;
 bool rendered_total=false;
 for(const auto&command:canvas.commands){
  if(command.kind=='R'&&command.x==95u&&command.w==1730u&&
     command.h==116u&&command.y>=400u&&command.y<950u){
   assert(command.y+command.h<=875u);
   ++visible_rows;
  }
  if(command.kind=='X'&&command.y==885u&&
     command.text.find("SHA-256")!=std::string::npos)
   rendered_total=true;
 }
 assert(visible_rows==4u&&rendered_total);
 bool valid_label=false;
 for(const auto&command:canvas.commands)
  if(command.kind=='X'&&command.text.find("CAP_0000000000000001  SHA-256 OK")!=
    std::string::npos)valid_label=true;
 assert(valid_label);
 private_inventory_selection=4u; // Fifth item appears on the next page.
 canvas.clear(Color::background);
 show_capsules(canvas);
 bool invalid_label=false;
 for(const auto&command:canvas.commands)
  if(command.kind=='X'&&
     command.text.find("CAP_0000000000000005  SHA NON VALIDE OU INACCESSIBLE")!=
       std::string::npos)invalid_label=true;
 assert(invalid_label);
 private_inventory_groups=true; // Restore selected group tab for pad test.
 private_archive_audit={};
 press(square);assert(screen==Screen::capsules);
 finish_scan();
 assert(!private_archive_audit.checked); // No real host /download0 workspace.
 press(l1);assert(!private_inventory_groups);
 press(circle);assert(screen==Screen::menu);
 mock_private_group_valid=false;
 mock_private_capsule_valid=false;
 assert(media_review.count==0u&&selected==0);
 /* One global Analyse coordinates independent, read-only scanners. */
 assert(selected==0);
 const unsigned engine_calls=mock_ampr_gets;
 press(cross);assert(screen==Screen::analyze);
 finish_scan();
 assert(general_audit_completed&&summary_status==Summary::ok);
 assert(homebrew_checked&&homebrew_stats.entries==8u);
 assert(autoload_checked&&autoload_stats.checked==11u);
 assert(general_games.checked&&general_games.image_files==3u&&
   general_games.folder_games==2u&&general_games.partial);
 assert(!duplicates_checked&&mock_ampr_gets==engine_calls);
 scene(canvas);
 press(r1);assert(analyze_page==1u);
 scene(canvas);
 press(l1);assert(analyze_page==0u);
 press(r1);assert(analyze_page==1u);
 press(cross);finish_scan();
 assert(general_audit_completed&&general_games.checked);
 assert(general_audit_stage.load()==6u);
 assert(mock_ampr_gets==engine_calls);
 /* Cancel the WHOLE general audit mid-Gallery scan, not just a standalone
  * media scan. No prior global module totals may remain published. */
 entered_stalled_scan.store(false);
 stall_next_scan.store(true);
 press(cross);
 const auto general_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(6);
 while(!entered_stalled_scan.load()&&
       std::chrono::steady_clock::now()<general_deadline)
  std::this_thread::yield();
 assert(entered_stalled_scan.load()&&task_busy());
 assert(general_audit_stage.load()==2u);
 press(circle);assert(screen==Screen::menu);
 finish_scan();
 assert(task_cancelled()&&general_audit_stage.load()==0u);
 assert(!general_audit_completed&&!general_games.checked);
 assert(!homebrew_checked&&!autoload_checked);
 assert(media_review.count==0u&&summary_status==Summary::unavailable);
 assert(media_review.entries[0].preview_path[0]==0);
 press(cross);assert(screen==Screen::analyze);
 finish_scan();
 assert(general_audit_completed&&general_audit_stage.load()==6u);
 press(circle);assert(screen==Screen::menu);
 analyze_page=0u;
 press(down);
 assert(selected==1);
 press(down);
 assert(selected==2);
 press(cross);
 assert(screen==Screen::cleanup);
 finish_scan();
 assert(summary_status==Summary::ok);
 assert(general_audit_completed&&homebrew_checked&&autoload_checked&&
   general_games.checked);
 assert(cleanup_page==0u);
 scene(canvas);
 press(r1);assert(cleanup_page==1u);
 scene(canvas);
 press(r1);assert(cleanup_page==2u);
 scene(canvas);
 bool app_cache_disclaimer=false;
 for(const auto&command:canvas.commands)
  if(command.text.find("INDICES TEMPORAIRES NON ATTESTES")!=std::string::npos)
   app_cache_disclaimer=true;
 assert(app_cache_disclaimer); // Installed apps are NOT cleanup proposals.
 assert(!cmp5_cleanup::assess(cmp5_cleanup::Evidence{
   cmp5_cleanup::Category::gallery,true,false,1u,0u}).can_delete);
 press(r1);assert(cleanup_page==3u);
 assert(general_games.image_nominal_bytes==38ULL*1024*1024*1024);
 assert(general_games.folder_nominal_bytes==54ULL*1024*1024*1024);
 assert(general_games.folder_partials==1u);
 scene(canvas);
 bool independent_measures=false, folder_measure=false;
 bool no_false_sum=false, root_partial=false;
 for(const auto&command:canvas.commands){
  if(command.text.find("38912 MIO IMAGES")!=std::string::npos)
   independent_measures=true;
  if(command.text.find("55296 MIO DOSSIERS")!=std::string::npos)
   folder_measure=true;
  if(command.text.find("1 DOSSIERS PARTIELS")!=std::string::npos)
   root_partial=true;
  if(command.text.find("VOLUMES NON ADDITIFS")!=std::string::npos)
   no_false_sum=true;
 }
 assert(independent_measures&&folder_measure&&root_partial&&no_false_sum);
 press(r1);assert(cleanup_page==0u);
 press(l1);assert(cleanup_page==3u);
 press(l1);assert(cleanup_page==2u);
 press(l1);assert(cleanup_page==1u);
 press(l1);assert(cleanup_page==0u);
 assert(!duplicates_checked);
 assert(scan_source==ScanSource::native_title);
 assert(media_review.count==2);
 scene(canvas);
 press(square);
 assert(screen==Screen::review);
 press(down);
 assert(review_selection==1);
 press(triangle);
 assert(review_sort==MediaSort::recent);
 assert(review_selection==0);
 press(r1);
 assert(review_type==MediaType::image && review_visible==1);
 press(r1);
 assert(review_type==MediaType::video && review_visible==1);
 press(r1);
 assert(review_type==MediaType::all && review_visible==2);
 press(cross);
 assert(review_visible==1);
 assert(std::strcmp(review_game,"GAME123")==0);
 press(cross);
 assert(review_visible==2 && !review_game[0]);
 press(r2);
 assert(review_visible==0);
 press(r2);
 assert(review_visible==2);
 press(l2);
 assert(review_visible==0);
 press(l2);
 assert(review_visible==2);
 press(right);
 assert(review_selection==1);
 press(left);
 assert(review_selection==0);
 // Full native keyboard search without PlayStation OS keyboard API.
 press(options);
 assert(screen==Screen::search);
 assert(review_visible==2);
 keyboard_position=15; // P
 press(cross);
 keyboard_position=7;  // H
 press(cross);
 assert(std::strcmp(review_query,"PH")==0);
 assert(review_visible==1);
 assert(review_order[0]==0);
 scene(canvas);
 press(triangle);
 assert(std::strcmp(review_query,"P")==0);
 assert(review_visible==1);
 press(square);
 assert(!review_query[0]&&review_visible==2);
 keyboard_position=6; // G: match both game IDs.
 press(cross);
 assert(review_visible==2);
 press(circle);
 assert(screen==Screen::review);
 assert(review_selection==0);
 // Group by real title ID and return to same catalog without rescanning.
 press(l3);
 assert(screen==Screen::games);
 assert(game_count==2 && !games_truncated);
 assert(std::strcmp(games[0].id,"GAME123")==0);
 assert(std::strcmp(games[0].name,"Astro's Playroom")==0);
 assert(games[0].count==1 && games[0].bytes==30ULL*1024*1024);
 press(down);
 assert(std::strcmp(games[game_selection].id,"GAME456")==0);
 press(square);
 assert(games[game_selection].marked==1&&marked_count==1);
 assert(marked_for_review[1]&&!marked_for_review[0]);
 press(square);
 assert(games[game_selection].marked==0&&marked_count==0);
 assert(!marked_for_review[1]);
 press(cross);
 assert(screen==Screen::review);
 assert(review_visible==1 && std::strcmp(review_game,"GAME456")==0);
 press(l3);
 assert(screen==Screen::games);
 press(triangle);
 assert(screen==Screen::review);
 assert(!review_game[0] && review_visible==2);
 press(options);
 assert(screen==Screen::search);
 press(square);
 assert(review_visible==2);
 press(circle);
 assert(screen==Screen::review);
 press(square);
 assert(screen==Screen::detail);
 assert(detail_index==review_order[review_selection]);
 press(square);
 assert(screen==Screen::companions);
 finish_scan();
 assert(companion_checked);
 assert(companion_summary.files==2u&&companion_summary.sidecars==1u);
 assert(companion_summary.media==1u);
 assert(companion_summary.state==CMP5_COMPANIONS_MULTIPLE);
 assert(companion_summary.nominal_bytes==30ULL*1024*1024+256u);
 scene(canvas);
 /* A visible companion group requires two affirmative controller actions.
  * Without a fresh source plan, it must fail BEFORE any private write. */
 press(triangle);assert(screen==Screen::group_backup&&group_step==0u);
 press(cross);assert(group_step==1u);
 press(circle);assert(screen==Screen::companions);
 press(triangle);assert(screen==Screen::group_backup&&group_step==0u);
 press(cross);assert(group_step==1u);
 press(cross);assert(group_step==2u);
 finish_scan();
 assert(group_outcome.status!=cmp5_private_backup::GroupStatus::verified);
 press(circle);assert(screen==Screen::companions);
 press(circle);
 assert(screen==Screen::detail);
 assert(!companion_checked&&!companion_summary.files);
 press(cross);
 assert(screen==Screen::photo);
 finish_scan();
 assert(photo_ready&&decoded_photo.width==32&&decoded_photo.height==16);
 assert(photo_zoom==100);
 press(r2);
 assert(photo_zoom==135);
 press(r2);
 assert(photo_zoom==170);
 press(l2);
 assert(photo_zoom==135);
 press(l2);
 assert(photo_zoom==100);
 press(r1); // Only one previewable image, must remain on it.
 finish_scan();
 assert(screen==Screen::photo&&photo_ready&&review_selection==0);
 assert(decoded_photo.pixels!=nullptr);
 scene(canvas);
 press(circle);
 assert(screen==Screen::detail);
 assert(!photo_ready&&decoded_photo.pixels==nullptr);
 /* A signed-off native private backup is a distinct operation from
  * selecting/marking media. Two separate X presses are mandatory.
  * This host fixture has no /download0 and cannot access a real Gallery. */
 auto&candidate=media_review.entries[detail_index];
 candidate.snapshot_device=42u;candidate.snapshot_inode=1234u;
 candidate.modified_at=1700000000;candidate.snapshot_ctime=1700000001;
 char backup_id[32]{};
 assert(cmp5_private_backup::capsule_name(candidate,backup_id));
 press(r2);assert(screen==Screen::backup&&backup_step==0u);
 press(cross);assert(backup_step==1u);
 press(circle);assert(screen==Screen::detail);
 press(r2);assert(screen==Screen::backup&&backup_step==0u);
 press(cross);assert(backup_step==1u);
 press(cross);assert(backup_step==2u);
 finish_scan();
 assert(backup_outcome.status!=cmp5_private_backup::Result::verified);
 press(circle);assert(screen==Screen::detail);
 /* After a UI restart the capsule identity is derived from the current
  * source snapshot; L2 checks storage without creating or writing files. */
 const unsigned checks_before=mock_private_verify_calls;
 press(l2);assert(screen==Screen::backup&&backup_verify_only);
 finish_scan();
 assert(mock_private_verify_calls==checks_before+1u);
 assert(backup_outcome.status==cmp5_private_backup::Result::verification_failed);
 press(circle);assert(screen==Screen::detail);
 mock_private_capsule_valid=true;
 press(l2);assert(screen==Screen::backup&&backup_verify_only);
 finish_scan();
 assert(mock_private_verify_calls==checks_before+2u);
 assert(backup_outcome.status==cmp5_private_backup::Result::verified);
 assert(backup_outcome.bytes==30ULL*1024*1024);
 press(circle);assert(screen==Screen::detail);
 /* Archived capsule selection is independent from the current Gallery
  * snapshot; synthetic checks have no real private volume or write calls. */
 press(l1);assert(screen==Screen::capsules);
 finish_scan();
 assert(!private_inventory.checked);
 private_inventory=cmp5_private_backup::Inventory{};
 private_inventory.checked=true;private_inventory.count=1u;
 std::strcpy(private_inventory.ids[0],backup_id);
 private_inventory_selection=0u;
 press(cross);assert(screen==Screen::backup&&backup_verify_only&&
                     backup_from_inventory);
 finish_scan();
 assert(backup_outcome.status==cmp5_private_backup::Result::verified);
 assert(mock_private_verify_calls==checks_before+3u);
 press(circle);assert(screen==Screen::capsules);
 press(circle);assert(screen==Screen::detail);
 mock_private_capsule_valid=false;
 /* Recreate the race where Circle navigates away while the preview
  * worker reports busy, then completes after the input handler returns. */
 const std::uint8_t late_frame[8]={0};
 assert(cmp5_decode_preview(late_frame,sizeof(late_frame),&decoded_photo));
 photo_ready=true;
 task_state.store(1,std::memory_order_release);
 scene(canvas);
 assert(photo_ready&&decoded_photo.pixels!=nullptr); // worker still active
 task_state.store(2,std::memory_order_release);
 scene(canvas);
 assert(!photo_ready&&decoded_photo.pixels==nullptr); // deferred disposal
 video_info=Cmp5VideoMetadata{95200,1920,1080,6};
 video_info_ready=true;
 scene(canvas);
 assert(!video_info_ready&&video_info.duration_ms==0);
 assert(marked_count==0);
 press(triangle);
 assert(marked_count==1);
 assert(marked_images==1&&marked_videos==0);
 assert(marked_nominal_bytes==30ULL*1024*1024);
 assert(marked_for_review[detail_index]);
 press(triangle);
 assert(marked_count==0&&marked_nominal_bytes==0);
 assert(marked_images==0&&marked_videos==0);
 press(triangle);
 assert(marked_count==1&&marked_nominal_bytes==30ULL*1024*1024);
 scene(canvas);
 press(circle);
 assert(screen==Screen::review);
 press(r3);
 assert(review_marked_only&&review_visible==1&&review_order[0]==0);
 press(square);
 assert(screen==Screen::detail&&detail_index==0);
 press(triangle);
 assert(marked_count==0&&review_visible==0);
 press(triangle);
 assert(marked_count==1&&review_visible==1&&review_order[0]==0);
 press(circle);
 assert(screen==Screen::review);
 press(r3);
 assert(!review_marked_only&&review_visible==2);
 press(down);
 assert(review_selection==1);
 press(square);
 assert(screen==Screen::detail && detail_index==1);
 press(triangle);
 assert(marked_count==2);
 assert(marked_images==1&&marked_videos==1);
 assert(marked_nominal_bytes==40ULL*1024*1024);
 press(triangle);
 assert(marked_count==1&&marked_nominal_bytes==30ULL*1024*1024);
 assert(marked_images==1&&marked_videos==0);
 press(cross);
 assert(screen==Screen::video_info);
 finish_scan();
 assert(video_info_ready);
 assert(video_info.duration_ms==95200&&video_info.width==1920&&video_info.height==1080);
 scene(canvas);
 press(circle);
 assert(screen==Screen::detail);
 assert(!video_info_ready&&video_info.duration_ms==0);
 press(square);
 assert(screen==Screen::companions);
 finish_scan();
 assert(companion_checked&&companion_summary.state==CMP5_COMPANIONS_SINGLE);
 assert(companion_summary.files==1u&&companion_summary.sidecars==0u);
 scene(canvas);
 press(circle);
 assert(screen==Screen::detail);
 // No locator and a cancelled request must fail closed, not appear single.
 const char saved=media_review.entries[detail_index].preview_path[0];
 media_review.entries[detail_index].preview_path[0]=0;
 press(square);
 assert(screen==Screen::companions);
 finish_scan();
 assert(!companion_checked&&!companion_summary.files);
 scene(canvas);
 press(circle);
 assert(screen==Screen::detail);
 media_review.entries[detail_index].preview_path[0]='!';
 press(square);
 assert(screen==Screen::companions);
 finish_scan();
 assert(!companion_checked&&!companion_summary.files);
 assert(companion_summary.failure==CMP5_COMPANION_FAILURE_DIRECTORY_LIMIT);
 scene(canvas);
 press(circle);
 assert(screen==Screen::detail);
 media_review.entries[detail_index].preview_path[0]=saved;
 press(circle);
 assert(screen==Screen::review);
 scene(canvas);
 press(circle);
 assert(screen==Screen::cleanup);
 assert(marked_count==1);
 assert(marked_nominal_bytes==30ULL*1024*1024);
 assert(marked_images==1&&marked_videos==0);
 press(triangle);
 finish_scan();
 assert(duplicates_checked && duplicate_stats.exact_groups==1);
 assert(duplicate_stats.matching_files==2);
 assert(duplicate_stats.duplicate_nominal_bytes==1024);
 scene(canvas);
 press(cross); // A fresh scan invalidates selected entries and duplicate snapshot.
 finish_scan();
 assert(marked_count==0 && !marked_for_review[0] && !marked_for_review[1]);
 assert(!review_marked_only);
 assert(marked_nominal_bytes==0&&marked_images==0&&marked_videos==0);
 assert(!duplicates_checked && duplicate_stats.exact_groups==0);
 press(circle);
 assert(screen==Screen::menu);
 press(down);
 assert(selected==3);
 press(cross);
 assert(screen==Screen::compression);
 finish_scan();
 assert(game_images_checked);
 assert(game_images.count[CMP5_IMAGE_EXFAT]==2u);
 assert(game_images.count[CMP5_IMAGE_FFPFSC]==1u);
 assert(game_images.shown_folders==2u);
 assert(!compression_engine_seen);
 press(r1);
 assert(compression_view==1u);
 scene(canvas);
 press(down);
 assert(compression_selection==1u);
 press(up);
 assert(compression_selection==0u);
 press(l1);
 assert(compression_view==0u);
 scene(canvas);
 mock_ampr_online=true;
 press(cross);
 finish_scan();
 assert(compression_engine_seen);
 assert(std::strcmp(compression_engine_version,"CleanMyPS5-1.0")==0);
 assert(mock_ampr_gets==1u);
 scene(canvas);
 press(triangle);
 assert(compression_view==5u);
 finish_scan();
 assert(game_progress_checked);
 assert(game_progress.state==cmp5_compression::JobState::idle);
 assert(game_progress.title_id[0]==0);
 assert(mock_ampr_gets==2u);
 scene(canvas);
 /* A reopened title can inspect existing checkpoint metadata without
  * issuing pause/resume/start or touching the saved job request body. */
 const unsigned controls_before=mock_ampr_pauses+mock_ampr_resumes+
   mock_ampr_post_headers+mock_ampr_post_bodies;
 press(square);
 assert(compression_view==7u);
 finish_scan();
 assert(recovery_checked);
 scene(canvas);
 assert(mock_ampr_pauses+mock_ampr_resumes+
        mock_ampr_post_headers+mock_ampr_post_bodies==controls_before);
 press(cross);finish_scan();
 assert(compression_view==7u&&recovery_checked);
 /* No in-memory checkpoint may authorize a POST without a fresh scan.
  * A recovery requires TWO separate controller confirmations. */
 recovery_inventory=Cmp5RecoveryInventory{};
 recovery_inventory.count=1u;
 std::strcpy(recovery_inventory.entries[0].title_id,"PPSA12345");
 std::strcpy(recovery_inventory.entries[0].destination,
             "/mnt/usb0/homebrew");
 recovery_inventory.entries[0].stage=CMP5_RECOVERY_PACK;
 recovery_checked=true;recovery_selection=0u;
 press(triangle);assert(compression_view==8u);
 press(circle);assert(compression_view==7u);
 press(triangle);assert(compression_view==8u);
 press(cross);assert(compression_view==9u);
 press(cross);assert(compression_view==10u);
 finish_scan();
 assert(!recovery_resume_attempted&&!recovery_resume_accepted&&
        !recovery_resume_unknown); /* no POST, so no retry lock */
 assert(mock_ampr_resumes==0u);
 assert(mock_ampr_pauses+mock_ampr_resumes+
        mock_ampr_post_headers+mock_ampr_post_bodies==controls_before);
 scene(canvas);
 press(circle);assert(compression_view==7u);
 press(triangle);assert(compression_view==8u); // safe NEW confirmation
 press(circle);assert(compression_view==7u);
 assert(mock_ampr_resumes==0u);
 recovery_resume_attempted=false;recovery_resume_accepted=false;
 recovery_resume_unknown=false;recovery_checked=false;
 recovery_inventory=Cmp5RecoveryInventory{};
 press(circle);assert(compression_view==5u);
 press(circle);
 assert(compression_view==0u); // Restore actual stats origin.
 press(triangle);finish_scan();
 assert(compression_view==5u&&compression_monitor_return_view==0u);
 press(r1);assert(compression_view==1u);
 press(triangle);finish_scan();
 assert(compression_monitor_return_view==1u);
 press(l1);assert(compression_view==0u);
 press(r1);assert(compression_view==1u);
 press(triangle);finish_scan();
 press(circle);assert(compression_view==1u); // Folder origin preserved.
 press(l1);
 assert(compression_view==0u);
 mock_ampr_online=false;
 press(cross);
 finish_scan();
 assert(!compression_engine_seen&&!compression_engine_version[0]);
 assert(mock_ampr_gets==5u); // Three extra read-only monitor checks.
 mock_ampr_online=true;
 press(r1);
 assert(compression_view==1u);
 press(right);
 assert(compression_destination==1u);
 press(cross);
 assert(compression_view==2u);
 finish_scan();
 assert(game_preflight_checked&&game_preflight_transport);
 assert(game_preflight.ready());
 assert(std::strcmp(game_preflight.title_id,"PPSA12345")==0);
 assert(std::strcmp(game_dest_checked,"/mnt/usb0/homebrew")==0);
 assert(mock_ampr_gets==6u);
 scene(canvas);
 press(cross);
 assert(compression_view==3u);
 scene(canvas);
 press(circle);
 assert(compression_view==1u); // backing out never submits work
 assert(mock_ampr_post_headers==0);
 press(cross);
 assert(compression_view==2u);
 finish_scan();
 assert(game_preflight.ready());
 /* Foreign running job and pending queue reject a newly requested game
  * despite a favorable per-title preflight. */
 mock_ampr_foreign_running=true;
 run_game_submission();
 assert(game_job_attempted&&!game_job_accepted);
 assert(mock_ampr_post_headers==0&&mock_ampr_post_bodies==0);
 mock_ampr_foreign_running=false;
 mock_ampr_queued_work=1u;
 run_game_submission();
 assert(!game_job_accepted);
 assert(mock_ampr_post_headers==0&&mock_ampr_post_bodies==0);
 mock_ampr_queued_work=0u;
 press(cross);
 assert(compression_view==3u);
 press(cross);
 assert(compression_view==4u);
 finish_scan();
 assert(game_job_attempted&&game_job_accepted);
 assert(!game_job_reply_unknown&&game_job_position==0u);
 assert(mock_ampr_post_headers==1&&mock_ampr_post_bodies==1);
 run_game_submission(); /* Accepted outcome is sticky: no repeat POST. */
 assert(game_job_accepted&&mock_ampr_post_headers==1&&mock_ampr_post_bodies==1);
 scene(canvas);
 press(cross);
 finish_scan();
 assert(game_progress_checked);
 assert(game_progress.state==cmp5_compression::JobState::running);
 assert(game_progress.percent==45u&&game_progress.bytes_done==450u);
 assert(std::strcmp(game_progress.title_id,"PPSA12345")==0);
 assert(mock_ampr_post_headers==1&&mock_ampr_post_bodies==1);
 /* The worker can pause itself when a source or output drive disappears.
  * A previously approved job remains eligible for an explicit resume
  * even though this UI never requested a pause. */
 mock_ampr_paused=true;
 press(cross);finish_scan();
 assert(game_progress_checked&&
        game_progress.state==cmp5_compression::JobState::paused);
 assert(game_progress.phase==cmp5_compression::JobPhase::paused);
 assert(game_progress.reason==cmp5_compression::PauseReason::destination_unavailable);
 assert(!game_pause_accepted&&!game_resume_accepted);
 press(triangle);finish_scan();
 assert(game_resume_attempted&&game_resume_accepted);
 assert(mock_ampr_resumes==1u&&!mock_ampr_paused);
 /* Stale PAUSED read must NOT trigger a second POST after accepted ACK. */
 press(triangle);finish_scan();
 assert(mock_ampr_resumes==1u);
 press(cross);finish_scan();
 assert(game_progress.state==cmp5_compression::JobState::running);
 assert(!game_resume_accepted&&!game_pause_accepted);
 press(triangle);
 finish_scan();
 assert(game_pause_attempted&&game_pause_accepted);
 assert(mock_ampr_pauses==1u);
 assert(mock_ampr_post_headers==1&&mock_ampr_post_bodies==1);
 press(cross);
 finish_scan();
 assert(game_progress_checked);
 assert(game_progress.state==cmp5_compression::JobState::paused);
 press(triangle);
 finish_scan();
 assert(game_resume_attempted&&game_resume_accepted);
 assert(mock_ampr_resumes==2u);
 assert(mock_ampr_post_headers==1&&mock_ampr_post_bodies==1);
 /* The engine has actually restarted the previous paused job. Do not
  * re-arm another resume until an explicit RUNNING status is observed. */
 press(cross);finish_scan();
 assert(game_progress_checked&&
        game_progress.state==cmp5_compression::JobState::running);
 assert(!game_resume_accepted);
 /* A recoverable error now keeps the partial directory intact.
  * Triangle may request a resume for THIS previously approved title;
  * it must not enqueue a fresh /api/start or delete the source. */
 mock_ampr_failed=true;
 press(cross);finish_scan();
 assert(game_progress_checked&&
   game_progress.state==cmp5_compression::JobState::error);
 press(triangle);finish_scan();
 assert(game_resume_attempted&&game_resume_accepted);
 assert(mock_ampr_resumes==3u);
 assert(mock_ampr_post_headers==1&&mock_ampr_post_bodies==1);
 mock_ampr_failed=false;
 scene(canvas);
 press(circle);
 assert(compression_view==1u);
 mock_ampr_online=false;
 press(cross);
 assert(compression_view==2u);
 finish_scan();
 assert(!game_preflight_checked&&!game_preflight.ready());
 press(circle);
 assert(compression_view==1u);
 press(l1);
 assert(compression_view==0u);
 press(square); /* no loader can be submitted without packaged ELF in host */
 finish_scan();
 assert(engine_launch==EngineLaunch::missing_file);
 assert(mock_ampr_gets==20u); // Auto-pause and confirmed restart probes.
 /* A real image requires a one-to-one SMP title identity at PRE and POST. */
 mock_ampr_online=true;
 mock_ampr_post_bodies=0u;
 mock_ampr_paused=false;
 press(l1);
 assert(compression_view==6u);
 press(triangle);finish_scan();
 assert(compression_view==5u&&compression_monitor_return_view==6u);
 press(circle);assert(compression_view==6u); // Image origin preserved.
 press(triangle);finish_scan();
 press(l1);assert(compression_view==1u);
 press(l1);assert(compression_view==0u);
 press(l1);assert(compression_view==6u);
 assert(game_images.shown_images==2u);
 assert(game_destination_count==17u); /* internal + USB0..7 + EXT0..7 */
 press(left);assert(compression_destination==0u);
 press(left);assert(compression_destination==16u);
 assert(std::strcmp(game_destinations[16],"/mnt/ext7/homebrew")==0);
 press(right);assert(compression_destination==0u);
 press(right);assert(compression_destination==1u);
 press(down);
 assert(compression_image_selection==1u);
 press(cross);
 assert(compression_view==6u); /* already packed .ffpfsc cannot be repacked */
 press(up);
 assert(compression_image_selection==0u);
 mock_smp_wrong_size=true;
 press(cross);
 assert(compression_view==2u);
 finish_scan();
 assert(!game_preflight_checked&&!game_preflight.ready());
 assert(!game_job_accepted&&mock_ampr_post_bodies==0u);
 press(cross);assert(compression_view==2u);
 press(circle);assert(compression_view==6u);
 mock_smp_wrong_size=false;
 mock_ampr_pending=true;
 press(cross);assert(compression_view==2u);
 finish_scan();
 assert(game_preflight_checked);
 assert(game_preflight.compatibility_pending&&!game_preflight.ready());
 assert(!game_job_accepted&&mock_ampr_post_bodies==0u);
 press(cross);assert(compression_view==2u);
 press(circle);assert(compression_view==6u);
 mock_ampr_pending=false;
 mock_ampr_image_bytes_inconsistent=true;
 press(cross);assert(compression_view==2u);
 finish_scan();
 assert(!game_preflight_checked&&!game_preflight.ready());
 assert(!game_job_accepted&&mock_ampr_post_bodies==0u);
 press(circle);assert(compression_view==6u);
 mock_ampr_image_bytes_inconsistent=false;
 press(cross);assert(compression_view==2u);
 finish_scan();
 assert(game_source_is_image);
 assert(game_preflight_checked&&game_preflight.ready());
 assert(game_image_expected_bytes==21ULL*1024*1024*1024);
 assert(std::strcmp(game_smp_title,"PPSA12345")==0);
 assert(std::strcmp(game_image_type,"exfatfs")==0);
 /* A file can mutate AFTER a favorable check but BEFORE confirmation.
  * No AMPR POST should be sent, even if the button was confirmed twice. */
 press(cross);assert(compression_view==3u);
 mock_smp_wrong_size=true;
 press(cross);assert(compression_view==4u);
 finish_scan();
 assert(game_job_attempted&&!game_job_accepted);
 assert(mock_ampr_post_bodies==0u);
 press(circle);assert(compression_view==6u);
 mock_smp_wrong_size=false;
 press(cross);assert(compression_view==2u);
 finish_scan();
 assert(game_preflight_checked&&game_preflight.ready());
 press(cross);assert(compression_view==3u);
 press(cross);assert(compression_view==4u);
 finish_scan();
 assert(game_job_accepted&&mock_ampr_post_bodies==1u);
 assert(mock_ampr_gets==29u); // Includes automatic pause, scoped resume and confirmed restart.
 press(circle);
 assert(compression_view==6u); /* return to selected image, not folders */
 /* Full UI->engine flow: a changed source length between confirmation
  * and submission must never result in a second AMPR POST. */
 mock_ampr_post_bodies=0u; // Simulate engine idle, no previous active work.
 press(cross);assert(compression_view==2u);
 finish_scan();
 assert(game_preflight_checked&&game_preflight.ready());
 assert(!game_job_plan_changed);
 press(cross);assert(compression_view==3u);
 mock_ampr_changed_source_bytes=true;
 press(cross);assert(compression_view==4u);
 finish_scan();
 assert(game_job_attempted&&!game_job_accepted&&!game_job_reply_unknown);
 assert(game_job_plan_changed);
 assert(mock_ampr_post_bodies==0u); // No bytes sent to /api/start.
 scene(canvas);
 mock_ampr_changed_source_bytes=false;
 press(circle);assert(compression_view==6u);
 press(r1);
 assert(compression_view==0u);
 mock_ampr_online=false;
 press(circle);
 assert(screen==Screen::menu);
 press(down);
 assert(selected==4);
 press(cross);
 assert(screen==Screen::official);
 press(cross);
 assert(official_checked[0]);
 press(down);
 press(cross);
 assert(official_checked[1]);
 scene(canvas);
 press(square);
 assert(screen==Screen::autoload);
 finish_scan();
 assert(autoload_checked);
 assert(autoload_stats.checked==11&&autoload_stats.present==5);
 assert(autoload_stats.missing==6&&autoload_stats.etahen_references==1);
 assert(autoload_stats.autostart_enabled==1&&autoload_stats.unreadable==1);
 scene(canvas);
 press(cross);
 finish_scan();
 assert(autoload_checked);
 press(circle);
 assert(screen==Screen::official);
 assert(official_checked[0]&&official_checked[1]);
 press(circle);
 assert(screen==Screen::menu);
 press(up);
 assert(selected==3);
 press(up);
 assert(selected==2);
 press(up);
 assert(selected==1);
 press(cross);
 assert(screen==Screen::access);
 finish_scan();
 assert(access_page==0u);
 scene(canvas);
 press(triangle);finish_scan();
 assert(helper_launch==EngineLaunch::missing_file);
 assert(engine!=Link::ok); // no false privilege elevation on missing asset
 press(cross);finish_scan(); // X must probe helper again, not only local paths
 assert(engine==Link::offline&&available==0u&&denied==0u&&absent==0u);
 scene(canvas);
 press(r1);assert(access_page==1u);
 scene(canvas);
 press(r1);assert(access_page==2u);
 scene(canvas);
 press(l1);assert(access_page==1u);
 press(l1);assert(access_page==0u);
 press(l1);assert(access_page==access_page_count-1u);
 scene(canvas);
 press(r1);assert(access_page==0u);
 press(square);
 assert(screen==Screen::apps);
 finish_scan();
 assert(homebrew_checked);
 assert(homebrew_stats.entries==8);
 assert(homebrew_stats.homebrew==3&&homebrew_stats.plugins==2);
 assert(homebrew_stats.installed==2&&homebrew_stats.app_data==1);
 assert(homebrew_stats.partial==1);
 assert(homebrew_stats.top_count<=CMP5_HOMEBREW_TOP_CAP);
 scene(canvas);
 assert(homebrew_page==0u);
 press(r1);assert(homebrew_page==1u);
 const auto before_mtime_ui=homebrew_stats;
 homebrew_stats.cache_like_folders=4u;
 homebrew_stats.cache_mtime_recent=1u;
 homebrew_stats.cache_mtime_unknown=1u;
 homebrew_stats.cache_mtime_no_recent=2u;
 scene(canvas);
 bool observed_recent=false,observed_unknown=false,no_idle_claim=false;
 for(const auto&command:canvas.commands){
  if(command.text.find("24H:1")!=std::string::npos)
   observed_recent=true;
  if(command.text.find("DATE?:1")!=std::string::npos)
   observed_unknown=true;
  if(command.text.find("INACTIVITE NON PROUVEE")!=std::string::npos)
   no_idle_claim=true;
 }
 assert(observed_recent&&observed_unknown&&no_idle_claim);
 /* Third tab exposes raw cache hints, no cleanup authorization. */
 homebrew_stats.cache_like_files=7u;
 homebrew_stats.cache_like_nominal_bytes=3145728ULL;
 homebrew_stats.cache_like_name_matched=2u;
 homebrew_stats.cache_like_name_unmatched=2u;
 homebrew_stats.homebrew_name_index_partial=1u;
 homebrew_stats.cache_like_partial=1u;
 homebrew_stats.data_root_state=3u;
 homebrew_stats.installed_root_state=4u;
 press(r1);assert(homebrew_page==2u);
 scene(canvas);
 bool cache_size=false,weak_match=false,index_partial=false;
 bool recency=false,blocked=false,cache_coverage_visible=false;
 for(const auto&command:canvas.commands){
  if(command.text.find("7 FICHIERS  3 MIO NOMINAUX")!=std::string::npos)
   cache_size=true;
  if(command.text.find("2 NOMS IDENTIQUES")!=std::string::npos)
   weak_match=true;
  if(command.text.find("INDEX PARTIEL")!=std::string::npos)
   index_partial=true;
  if(command.text.find("1 MODIF 24H  1 DATE INCONNUE")!=std::string::npos)
   recency=true;
  if(command.text.find("PROCESSUS NON VERIFIES")!=std::string::npos)
   blocked=true;
  if(command.text.find("/DATA:REFUSE  TITRES:ERREUR")!=std::string::npos)
   cache_coverage_visible=true;
 }
 assert(cache_size&&weak_match&&index_partial&&recency&&blocked&&cache_coverage_visible);
 /* A fourth page reviews the largest individual cache-shaped folders.
  * It never reveals full paths or offers a destructive action. */
 homebrew_stats.cache_top_count=2u;
 homebrew_stats.cache_top[0]=Cmp5CacheTop{};
 homebrew_stats.cache_top[0].nominal_bytes=3145728u;
 homebrew_stats.cache_top[0].files=7u;
 homebrew_stats.cache_top[0].kind=1u;
 homebrew_stats.cache_top[0].age_hint=1u;
 std::strcpy(homebrew_stats.cache_top[0].app_label,"ExampleTool");
 homebrew_stats.cache_top[1]=Cmp5CacheTop{};
 homebrew_stats.cache_top[1].nominal_bytes=9u;
 homebrew_stats.cache_top[1].kind=3u;
 homebrew_stats.cache_top[1].age_hint=0u;
 homebrew_stats.cache_top[1].partial=1u;
 std::strcpy(homebrew_stats.cache_top[1].app_label,"OtherApp");
 press(r1);assert(homebrew_page==3u);
 scene(canvas);
 bool listed_cache=false,listed_temp=false,blocked_delete=false;
 for(const auto&command:canvas.commands){
  if(command.text.find("ExampleTool / CACHE")!=std::string::npos&&
     command.text.find("1  ExampleTool")!=std::string::npos)
   listed_cache=true;
  if(command.text.find("OtherApp / TMP")!=std::string::npos)
   listed_temp=true;
  if(command.text.find("AUCUN CACHE SUPPRIMABLE")!=std::string::npos)
   blocked_delete=true;
 }
 assert(listed_cache&&listed_temp&&blocked_delete);
 /* Both directions wrap all four tabs, including zero candidates. */
 homebrew_stats.cache_like_files=0u;
 homebrew_stats.cache_like_folders=0u;
 homebrew_stats.cache_like_nominal_bytes=0u;
 homebrew_stats.cache_like_partial=0u;
 press(l1);assert(homebrew_page==2u);
 scene(canvas);
 bool empty_cache=false;
 for(const auto&command:canvas.commands)
  if(command.text.find("0 DOSSIERS  0 FICHIERS  0 OCTETS NOMINAUX")
     !=std::string::npos)empty_cache=true;
 assert(empty_cache);
 press(r1);assert(homebrew_page==3u);
 press(r1);assert(homebrew_page==0u);
 press(l1);assert(homebrew_page==3u);
 press(l1);assert(homebrew_page==2u);
 press(l1);assert(homebrew_page==1u);
 homebrew_stats=before_mtime_ui;
 press(l1);assert(homebrew_page==0u);
 press(circle);
 assert(screen==Screen::access);
 press(circle);
 assert(screen==Screen::menu);
 // A long-running scan must respond to Circle without freezing navigation
 // or exposing a half-published result, and must not authorize another scan
 // until it has acknowledged the cancellation.
 selected=2;
 entered_stalled_scan.store(false);
 stall_next_scan.store(true);
 press(cross);
 assert(screen==Screen::cleanup);
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(6);
 while(!entered_stalled_scan.load()&&std::chrono::steady_clock::now()<deadline)
  std::this_thread::yield();
 assert(entered_stalled_scan.load());
 assert(task_busy());
 press(circle);
 assert(screen==Screen::menu);
 assert(scan_cancel_requested.load());
 finish_scan();
 assert(task_cancelled());
 assert(media_review.count==0 && summary_status==Summary::unavailable);
 assert(media_review.entries[0].preview_path[0]==0);
 press(cross);
 assert(screen==Screen::cleanup);
 finish_scan();
 assert(!task_cancelled() && summary_status==Summary::ok);
 // Regression: malformed or failed scans may populate Cmp5MediaReview
 // temporarily, but never publish stale or partially valid rows.
 fail_next_overview.store(true);
 press(cross);
 finish_scan();
 assert(summary_status==Summary::unavailable);
 assert(scan_source==ScanSource::none);
 assert(media_review.count==0&&review_visible==0&&game_count==0);
 assert(media_review.entries[0].preview_path[0]==0);
 assert(marked_count==0&&marked_nominal_bytes==0);
 press(square);
 assert(screen==Screen::cleanup);
 corrupt_next_overview.store(true);
 press(cross);
 finish_scan();
 assert(summary_status==Summary::unavailable);
 assert(media_review.count==0&&review_visible==0);
 assert(media_review.entries[0].preview_path[0]==0);
 press(square);
 assert(screen==Screen::cleanup);
 press(cross);
 finish_scan();
 assert(summary_status==Summary::ok&&media_review.count==2);
 // A helper must not override the trusted native summary when a "files"
 // number appears only in an unrelated nested JSON object.
 const char nested_spoof[]=R"({"schemaVersion":1,"mode":"metadata-only","scope":"bounded-known-roots",)"
   R"("cleanupValidated":false,"nominalBytes":40,"partial":false,)"
   R"("categories":{"media":2,"logs":0,"cache":0,"files":2},)"
   R"("review":{"largeMedia":0,"oldMedia":0,"oldLargeMedia":0,)"
   R"("largeMediaNominalBytes":0,"validatedForDeletion":false}})";
 const auto prior_source=scan_source;
 const auto prior_count=summary_files;
 assert(!accept_overview(nested_spoof,ScanSource::helper));
 assert(scan_source==prior_source&&summary_files==prior_count);
 assert(summary_status==Summary::ok);
 // Helper aggregates must be stored separately: they cannot replace a
 // native scan, its source identity, or the concrete media review catalog.
 const char valid_helper[]=R"({"schemaVersion":1,"mode":"metadata-only",)"
   R"("scope":"bounded-known-roots","cleanupValidated":false,)"
   R"("files":120,"nominalBytes":9000000,"partial":true,)"
   R"("categories":{"media":30,"logs":4,"cache":20},)"
   R"("review":{"largeMedia":3,"oldMedia":12,"oldLargeMedia":2,)"
   R"("largeMediaNominalBytes":5000000,"validatedForDeletion":false}})";
 const auto previous_native_count=media_review.count;
 assert(accept_helper_overview(valid_helper));
 assert(helper_summary_checked&&helper_summary.files==120u);
 assert(summary_status==Summary::ok&&scan_source==prior_source);
 assert(summary_files==prior_count&&media_review.count==previous_native_count);
 assert(!accept_helper_overview(nested_spoof));
 assert(!helper_summary_checked&&helper_summary.files==0u);
 assert(summary_files==prior_count&&media_review.count==previous_native_count);
 /* Saturation must not wrap nominal selection totals to a small value.
  * Deselecting then recalculating restores the exact remaining total. */
 assert(media_review.count==2);
 const auto saved_first=media_review.entries[0].bytes;
 const auto saved_second=media_review.entries[1].bytes;
 media_review.entries[0].bytes=UINT64_MAX-5u;
 media_review.entries[1].bytes=50;
 marked_for_review[0]=true;
 marked_for_review[1]=true;
 rebuild_marked_totals();
 assert(marked_count==2&&marked_images==1&&marked_videos==1);
 assert(marked_nominal_bytes==UINT64_MAX);
 marked_for_review[1]=false;
 rebuild_marked_totals();
 assert(marked_nominal_bytes==UINT64_MAX-5u&&marked_count==1);
 marked_for_review[0]=false;
 media_review.entries[0].bytes=saved_first;
 media_review.entries[1].bytes=saved_second;
 rebuild_marked_totals();
 assert(marked_count==0&&marked_nominal_bytes==0);
 // A partially marked game toggles to ALL of its scanned media, then to
 // NONE, regardless of the current type/search filters in the catalogue.
 media_review.entries[2]=media_review.entries[0];
 media_review.count=3;
 media_review.media_scanned=3;
 marked_for_review[0]=true;
 review_type=MediaType::video;
 review_query[0]='Z';review_query[1]=0;
 rebuild_marked_totals();
 rebuild_games();
 assert(game_count==2&&std::strcmp(games[0].id,"GAME123")==0);
 assert(games[0].count==2&&games[0].marked==1);
 toggle_game_marks(0);
 assert(games[0].marked==2&&marked_count==2);
 assert(marked_for_review[0]&&marked_for_review[2]&&!marked_for_review[1]);
 assert(marked_nominal_bytes==60ULL*1024*1024);
 toggle_game_marks(0);
 assert(games[0].marked==0&&marked_count==0);
 assert(!marked_for_review[0]&&!marked_for_review[2]);
 toggle_game_marks(game_count); // Invalid selection must be a no-op.
 assert(marked_count==0);
 media_review.count=2;media_review.media_scanned=2;
 media_review.entries[2]=Cmp5MediaCandidate{};
 marked_for_review[2]=false;
 review_type=MediaType::all;review_query[0]=0;
 rebuild_review_index();
 press(circle);
 assert(screen==Screen::menu);
 // Produce visually inspectable previews using the ACTUAL native scene().
 // All data below is synthetic, not the user's files or PS5 logs.
 if(const char*directory=std::getenv("CMP5_NATIVE_PREVIEW_DIR")){
  const auto save=[&](Screen display,const char*name){
   screen=display;
   scene(canvas);
   char filepath[320]{};
   (void)std::snprintf(filepath,sizeof(filepath),"%s/%s.trace",directory,name);
   assert(canvas.save_trace(filepath));
  };
  selected=2;
  save(Screen::menu,"01-menu");
  /* Library preview is independent from Gallery discovery. The archive
   * identifiers are synthetic and do not represent the user's captures. */
  capsule_return_screen=Screen::menu;
  private_inventory=cmp5_private_backup::Inventory{};
  private_inventory.checked=true;private_inventory.count=2u;
  private_inventory.partial=true;private_inventory.staged=2u;
  private_inventory.staged_unverified=1u;
  private_inventory.staged_nominal_bytes=15ULL*1024ULL*1024ULL;
  std::strcpy(private_inventory.ids[0],"CAP_0123456789ABCDEF");
  std::strcpy(private_inventory.ids[1],"CAP_9999999999999999");
  private_inventory_selection=0u;
  save(Screen::capsules,"01a-home-private-archives");
  bool staging_banner=false;
  for(const auto&command:canvas.commands)
   if(command.text.find("2 TEMPORAIRES")!=std::string::npos&&
      command.text.find("15 MIO")!=std::string::npos)
    staging_banner=true;
  assert(staging_banner);
  private_rehearsal_step=1u;
  private_rehearsal_outcome={};
  save(Screen::rehearsal,"01ab-private-rehearsal-confirm");
  private_rehearsal_step=2u;
  private_rehearsal_outcome.status=
    cmp5_private_backup::RehearsalStatus::verified;
  private_rehearsal_outcome.bytes=30ULL*1024*1024;
  save(Screen::rehearsal,"01ac-private-rehearsal-complete");
  private_rehearsal_discard=true;
  private_rehearsal_verify_only=false;
  private_rehearsal_groups=false;
  private_rehearsal_step=1u;
  private_rehearsal_outcome={};
  save(Screen::rehearsal,"01ad-private-raw-cleanup-confirm");
  private_rehearsal_step=2u;
  private_rehearsal_outcome.status=
    cmp5_private_backup::RehearsalStatus::verified;
  private_rehearsal_outcome.bytes=30ULL*1024*1024;
  save(Screen::rehearsal,"01ae-private-raw-cleanup-complete");
  bool raw_cleanup_banner=false;
  for(const auto&command:canvas.commands)
   if(command.text.find("COPIE PRIVEE RETIREE")!=std::string::npos)
    raw_cleanup_banner=true;
  assert(raw_cleanup_banner);
  /* A post-unlink fsync failure must NEVER render a clean refusal or
   * report the target as definitely still present. */
  private_rehearsal_outcome.status=
    cmp5_private_backup::RehearsalStatus::indeterminate;
  scene(canvas);
  bool uncertain_banner=false,ambiguous_detail=false;
  for(const auto&command:canvas.commands){
   if(command.text.find("ETAT DE RETRAIT INCERTAIN")!=std::string::npos)
    uncertain_banner=true;
   if(command.text.find("SUPPRESSION TENTEE")!=std::string::npos)
    ambiguous_detail=true;
  }
  assert(uncertain_banner&&ambiguous_detail);
  screen=Screen::capsules;
  scene(canvas);
  bool archive_warning=false;
  for(const auto&command:canvas.commands)
   if(command.text.find("RETRAIT R3 INCERTAIN")!=std::string::npos)
    archive_warning=true;
  assert(archive_warning);
  screen=Screen::rehearsal;
  /* A late Circle event can set the task to cancelled after unlink
   * confirmation. A recorded success is still a success, not an error. */
  private_rehearsal_outcome.status=
    cmp5_private_backup::RehearsalStatus::verified;
  task_state.store(4u,std::memory_order_release);
  scene(canvas);
  bool successful_late_cancel=false;
  for(const auto&command:canvas.commands)
   if(command.text.find("COPIE PRIVEE RETIREE")!=std::string::npos)
    successful_late_cancel=true;
  assert(successful_late_cancel);
  screen=Screen::capsules;
  scene(canvas);
  bool archive_confirmed=false;
  for(const auto&command:canvas.commands)
   if(command.text.find("RETRAIT R3 CONFIRME")!=std::string::npos)
    archive_confirmed=true;
  assert(archive_confirmed);
  screen=Screen::rehearsal;
  task_state.store(2u,std::memory_order_release);
  private_rehearsal_discard=false;
  private_rehearsal_step=0u;
  private_rehearsal_outcome={};
  private_inventory.group_count=1u;
  std::strcpy(private_inventory.group_ids[0],"GRP_0123456789ABCDEF");
  private_inventory_groups=true;
  save(Screen::capsules,"01aa-home-group-archives");
  private_inventory_groups=false;
  private_inventory={};private_inventory_selection=0u;
  capsule_return_screen=Screen::detail;
  // These native UI captures are synthetic and certify only rendering
  // across both pages, not PS5 device coverage or filesystem permissions.
  general_audit_completed=true;
  general_games=GeneralGameAudit{};
  general_games.checked=true;general_games.partial=true;
  general_games.image_files=3u;general_games.folder_games=2u;
  general_games.scanned_roots=2u;general_games.unavailable_roots=1u;
  homebrew_checked=true;homebrew_stats=Cmp5HomebrewStats{};
  homebrew_stats.homebrew=3u;homebrew_stats.plugins=2u;
  homebrew_stats.app_data=1u;homebrew_stats.installed=2u;
  homebrew_stats.partial=1u;
  autoload_checked=true;autoload_stats=Cmp5AutoloadStats{};
  autoload_stats.checked=11u;autoload_stats.present=5u;
  autoload_stats.unreadable=1u;autoload_stats.oversized=1u;
  analyze_page=0u;
  save(Screen::analyze,"01g-general-analysis");
  analyze_page=1u;
  save(Screen::analyze,"01h-general-modules");
  analyze_page=0u;
  save(Screen::compression,"01b-compression");
  compression_view=1;compression_selection=0;
  save(Screen::compression,"01c-compression-games");
  compression_view=6u;compression_image_selection=0;
  save(Screen::compression,"01e-compression-images");
  compression_view=2u;
  save(Screen::compression,"01f-compression-image-check");
  compression_view=5u;
  game_progress_checked=true;
  game_progress=cmp5_compression::GameProgress{};
  game_progress.state=cmp5_compression::JobState::running;
  std::strcpy(game_progress.title_id,"PPSA12345");
  game_progress.percent=45;
  game_progress.bytes_done=450ULL*1024*1024;
  game_progress.bytes_total=1000ULL*1024*1024;
  save(Screen::compression,"01d-compression-monitor");
  /* The offline checkpoint view renders from synthetic title identifiers.
   * A recorded stage is not authorization to resume the game. */
  recovery_inventory=Cmp5RecoveryInventory{};
  recovery_inventory.count=1u;
  std::strcpy(recovery_inventory.entries[0].title_id,"PPSA12345");
  std::strcpy(recovery_inventory.entries[0].destination,"/mnt/usb0/homebrew");
  recovery_inventory.entries[0].stage=CMP5_RECOVERY_COPY;
  recovery_checked=true;recovery_selection=0u;
  compression_view=7u;
  save(Screen::compression,"01i-compression-recovery");
  recovery_chosen=recovery_inventory.entries[0];
  compression_view=8u;
  save(Screen::compression,"01j-recovery-confirm-1");
  compression_view=9u;
  save(Screen::compression,"01k-recovery-confirm-2");
  compression_view=10u;
  recovery_resume_attempted=true;
  recovery_resume_accepted=false;recovery_resume_unknown=true;
  save(Screen::compression,"01l-recovery-reply-unknown");
  recovery_resume_attempted=false;recovery_resume_unknown=false;
  compression_view=7u;
  recovery_checked=false;
  game_progress_checked=false;
  compression_view=0;
  cleanup_page=0u;
  save(Screen::cleanup,"02-cleanup");
  general_audit_completed=true;
  general_games=GeneralGameAudit{};
  general_games.checked=true;general_games.partial=true;
  general_games.folder_games=2u;general_games.image_files=3u;
  general_games.unavailable_roots=1u;
  homebrew_checked=true;homebrew_stats=Cmp5HomebrewStats{};
  homebrew_stats.homebrew=3u;homebrew_stats.plugins=2u;
  homebrew_stats.app_data=1u;homebrew_stats.installed=2u;
  autoload_checked=true;autoload_stats=Cmp5AutoloadStats{};
  autoload_stats.checked=11u;autoload_stats.present=5u;
  autoload_stats.unreadable=1u;
  cleanup_page=1u;
  save(Screen::cleanup,"02b-cleanup-domains");
  cleanup_page=2u;
  save(Screen::cleanup,"02c-cleanup-readiness");
  cleanup_page=3u;
  save(Screen::cleanup,"02d-cleanup-storage-volumes");
  cleanup_page=0u;
  review_query[0]=0;review_game[0]=0;
  review_type=MediaType::all;review_older=false;review_large=false;
  review_sort=MediaSort::size;review_selection=0;
  rebuild_review_index();
  save(Screen::review,"03-catalog");
  marked_for_review[0]=true;
  rebuild_marked_totals();
  review_marked_only=true;
  rebuild_review_index();
  save(Screen::review,"03b-marked-catalog");
  review_marked_only=false;
  marked_for_review[0]=false;
  rebuild_marked_totals();
  rebuild_review_index();
  save(Screen::search,"04-search");
  rebuild_games();game_selection=0;
  save(Screen::games,"05-games");
  marked_for_review[0]=true;
  rebuild_marked_totals();rebuild_games();game_selection=0;
  save(Screen::games,"05b-games-marked");
  marked_for_review[0]=false;
  rebuild_marked_totals();rebuild_games();
  detail_index=0;
  save(Screen::detail,"06-details");
  companion_summary=Cmp5CompanionReport{CMP5_COMPANIONS_MULTIPLE,CMP5_COMPANION_FAILURE_NONE,
    CMP5_THUMBNAIL_CANDIDATE_FOUND,2u,1u,1u,30ULL*1024*1024+256u};
  companion_checked=true;
  save(Screen::companions,"06b-companions");
  group_verify_only=false;group_step=0u;
  save(Screen::group_backup,"06ba-group-confirm");
  group_step=2u;
  group_outcome=cmp5_private_backup::GroupOutcome{};
  group_outcome.status=cmp5_private_backup::GroupStatus::verified;
  group_outcome.receipt.count=3u;
  group_outcome.receipt.bytes=30ULL*1024*1024+256u;
  save(Screen::group_backup,"06bb-group-verified");
  group_outcome={};group_step=0u;
  companion_summary=Cmp5CompanionReport{CMP5_COMPANIONS_SINGLE,CMP5_COMPANION_FAILURE_NONE,
    CMP5_THUMBNAIL_KNOWN_PATH_ABSENT,1u,1u,0u,10ULL*1024*1024};
  save(Screen::companions,"06c-single-capture");
  companion_summary=Cmp5CompanionReport{};
  companion_summary.failure=CMP5_COMPANION_FAILURE_DIRECTORY_LIMIT;
  companion_checked=false;
  save(Screen::companions,"06d-companion-limit");
  companion_summary=Cmp5CompanionReport{};
  companion_checked=false;
  save(Screen::official,"07-official");
  autoload_checked=true;
  autoload_stats=Cmp5AutoloadStats{};
  autoload_stats.checked=11;autoload_stats.present=5;
  autoload_stats.missing=6;autoload_stats.etahen_references=1;
  autoload_stats.autostart_enabled=1;autoload_stats.unreadable=1;
  save(Screen::autoload,"07b-autoload");
  save(Screen::access,"08-access");
  save(Screen::apps,"09-apps");
  const std::uint8_t synthetic_photo[8]={0};
  assert(cmp5_decode_preview(synthetic_photo,sizeof(synthetic_photo),&decoded_photo));
  photo_ready=true;
  save(Screen::photo,"10-photo");
  cmp5_release_preview(&decoded_photo);
  photo_ready=false;
  video_info_ready=true;
  video_info=Cmp5VideoMetadata{95200,1920,1080,6};
  save(Screen::video_info,"11-video-info");
 }
 /* A lost ACK after the body was delivered may already have triggered
  * the checkpoint pause/resume. Neither operation can be replayed solely
  * because its response could not be read. */
 mock_ampr_online=true;
 mock_ampr_post_bodies=1u;mock_ampr_queued_work=0u;
 mock_ampr_failed=false;mock_ampr_paused=false;
 game_preflight=cmp5_compression::GamePreflight{};
 std::strcpy(game_preflight.title_id,"PPSA12345");
 game_preflight.compatible=true;game_preflight.source_bytes=1u;
 game_preflight.need_max=1u;game_preflight.dest_free=2u;
 game_preflight_checked=true;game_job_accepted=true;
 game_source_is_image=false;
 std::strcpy(game_dest_checked,"/mnt/usb0/homebrew");
 reset_compression_job_state();
 game_preflight=cmp5_compression::GamePreflight{};
 std::strcpy(game_preflight.title_id,"PPSA12345");
 game_preflight.compatible=true;game_preflight.source_bytes=1u;
 game_preflight.need_max=1u;game_preflight.dest_free=2u;
 game_preflight_checked=true;game_job_accepted=true;
 const unsigned pauses_before=mock_ampr_pauses;
 const unsigned resumes_before=mock_ampr_resumes;
 mock_ampr_control_drop_reply=true;
 run_game_pause();
 assert(game_pause_attempted&&game_pause_unknown&&!game_pause_accepted);
 assert(mock_ampr_pauses==pauses_before+1u&&mock_ampr_paused);
 run_game_pause();
 assert(mock_ampr_pauses==pauses_before+1u);
 run_game_resume();
 assert(game_resume_attempted&&game_resume_unknown&&!game_resume_accepted);
 assert(mock_ampr_resumes==resumes_before+1u&&!mock_ampr_paused);
 run_game_resume();
 assert(mock_ampr_resumes==resumes_before+1u);
 mock_ampr_control_drop_reply=false;
 run_game_progress();
 assert(game_progress_checked&&game_progress.state==cmp5_compression::JobState::running);
 assert(game_resume_unknown&&game_pause_unknown);
 run_game_resume();run_game_pause();
 assert(mock_ampr_resumes==resumes_before+1u&&mock_ampr_pauses==pauses_before+1u);
 reset_compression_job_state();
 assert(!game_pause_unknown&&!game_resume_unknown);
 const unsigned resume_count=mock_ampr_resumes;
 run_game_resume(); /* No unknown checkpoint is auto-adopted. */
 assert(mock_ampr_resumes==resume_count);
 return 0;
}
