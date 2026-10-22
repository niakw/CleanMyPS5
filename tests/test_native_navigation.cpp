// Host-only navigation test for PS5-native UI, no console and no filesystem writes.
#define main cmp5_native_app_entry
#include "../native/main.cpp"
#undef main

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {
std::uint32_t injected_buttons=0;
std::atomic<bool> stall_next_scan{false};
std::atomic<bool> entered_stalled_scan{false};

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
int sceNetSocket(const char*,int,int,int){return -1;} // No optional helper in host smoke test
int sceNetConnect(int,const void*,std::uint32_t){return -1;}
int sceNetSend(int,const void*,std::size_t,int){return -1;}
int sceNetRecv(int,void*,std::size_t,int){return -1;}
int sceNetSetsockopt(int,int,int,const void*,std::uint32_t){return -1;}
int sceNetSocketClose(int){return 0;}

char *cmp5_overview_with_review(const char*,std::size_t*len,Cmp5MediaReview*review){
 // Synthetic fixed result: the native title can work without any helper.
 const char*response=R"({"schemaVersion":1,"scope":"bounded-known-roots","cleanupValidated":false,"files":2,"visited":4,"nominalBytes":40000000,"partial":true,"categories":{"media":2,"logs":0,"cache":0,"residuals":0,"other":0},"review":{"largeMedia":0,"oldMedia":2,"oldLargeMedia":0,"largeMediaNominalBytes":0,"validatedForDeletion":false}})";
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
 return cmp5_overview_with_review(root,len,review);
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
}

namespace ps5::demo {
[[noreturn]] void run(DrawScene,std::string_view) noexcept {std::abort();}
}

int main(){
 controller=1;
 ps5::demo::Canvas canvas;
 assert(screen==Screen::menu);
 press(down);
 assert(selected==1);
 press(down);
 assert(selected==2);
 press(cross);
 assert(screen==Screen::cleanup);
 finish_scan();
 assert(summary_status==Summary::ok);
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
 scene(canvas);
 press(circle);
 assert(screen==Screen::detail);
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
 press(down);
 assert(review_selection==1);
 press(square);
 assert(screen==Screen::detail && detail_index==1);
 press(cross);
 assert(screen==Screen::video_info);
 finish_scan();
 assert(video_info_ready);
 assert(video_info.duration_ms==95200&&video_info.width==1920&&video_info.height==1080);
 scene(canvas);
 press(circle);
 assert(screen==Screen::detail);
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
 assert(marked_nominal_bytes==0&&marked_images==0&&marked_videos==0);
 assert(!duplicates_checked && duplicate_stats.exact_groups==0);
 press(circle);
 assert(screen==Screen::menu);
 press(down);
 assert(selected==3);
 press(cross);
 assert(screen==Screen::official);
 press(cross);
 assert(official_checked[0]);
 press(down);
 press(cross);
 assert(official_checked[1]);
 scene(canvas);
 press(circle);
 assert(screen==Screen::menu);
 press(up);
 assert(selected==2);
 press(up);
 assert(selected==1);
 press(cross);
 assert(screen==Screen::access);
 finish_scan();
 scene(canvas);
 press(square);
 assert(screen==Screen::apps);
 finish_scan();
 assert(homebrew_checked);
 assert(homebrew_stats.entries==8);
 assert(homebrew_stats.homebrew==3&&homebrew_stats.plugins==2);
 assert(homebrew_stats.installed==2&&homebrew_stats.app_data==1);
 assert(homebrew_stats.partial==1);
 scene(canvas);
 press(circle);
 assert(screen==Screen::access);
 press(circle);
 assert(screen==Screen::menu);
 // A long-running scan must respond to Circle without freezing navigation
 // or exposing a half-published result, and must not authorize another scan
 // until it has acknowledged the cancellation.
 selected=2;
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
 press(cross);
 assert(screen==Screen::cleanup);
 finish_scan();
 assert(!task_cancelled() && summary_status==Summary::ok);
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
  save(Screen::cleanup,"02-cleanup");
  review_query[0]=0;review_game[0]=0;
  review_type=MediaType::all;review_older=false;review_large=false;
  review_sort=MediaSort::size;review_selection=0;
  rebuild_review_index();
  save(Screen::review,"03-catalog");
  save(Screen::search,"04-search");
  rebuild_games();game_selection=0;
  save(Screen::games,"05-games");
  detail_index=0;
  save(Screen::detail,"06-details");
  save(Screen::official,"07-official");
  save(Screen::access,"08-access");
  save(Screen::apps,"09-apps");
  photo_ready=true;
  save(Screen::photo,"10-photo");
  video_info_ready=true;
  video_info=Cmp5VideoMetadata{95200,1920,1080,6};
  save(Screen::video_info,"11-video-info");
 }
 return 0;
}
