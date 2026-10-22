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
 review->entries[0].bytes=30ULL*1024*1024;
 review->entries[0].media_type=1;
 std::strcpy(review->entries[1].label,"VIDEO DEUX");
 std::strcpy(review->entries[1].title_id,"GAME456");
 review->entries[1].bytes=10ULL*1024*1024;
 review->entries[1].media_type=2;
 return out;
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
 press(square);
 assert(screen==Screen::detail);
 assert(detail_index==review_order[review_selection]);
 scene(canvas);
 press(circle);
 assert(screen==Screen::review);
 scene(canvas);
 press(circle);
 assert(screen==Screen::cleanup);
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
 press(circle);
 assert(screen==Screen::menu);
 return 0;
}
