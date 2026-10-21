// CleanMyPS5 native dashboard prototype (GPL-3.0-or-later).
// Uses the independently built VideoOut renderer of the pinned native toolchain.
// Designed to run as a PS5 title, never by opening a web browser.
#include "demo_renderer.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

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

constexpr std::uint32_t up=0x10,down=0x40,cross=0x4000,circle=0x2000;
int controller=-1,selected=0;
std::uint32_t previous=0;
enum class Link { unchecked, ok, offline, invalid };
Link state=Link::unchecked;
unsigned available=0,denied=0,absent=0;
constexpr int row_count=4;
using Color=ps5::demo::Color;

bool scan_field(const char*text,const char*name,unsigned*out) noexcept {
 const char*start=std::strstr(text,name);
 if(!start)return false;
 start+=std::strlen(name);
 unsigned n=0;
 if(*start<'0'||*start>'9')return false;
 while(*start>='0'&&*start<='9') {
  const unsigned d=static_cast<unsigned>(*start-'0');
  if(n>100000u)return false;
  n=n*10u+d;start++;
 }
 *out=n;return true;
}
bool get_preflight() noexcept {
 int socket=sceNetSocket("cleanmyps5_ui",2,1,6);
 if(socket<0)return false;
 constexpr int timeout_us=350000;
 // Official sceNet socket timeout options as used by the community SDK.
 (void)sceNetSetsockopt(socket,0xffff,0x1105,&timeout_us,sizeof(timeout_us));
 (void)sceNetSetsockopt(socket,0xffff,0x1106,&timeout_us,sizeof(timeout_us));
 const NetSockaddrIn local{16,2,static_cast<std::uint16_t>((8877u<<8)|(8877u>>8)),0x0100007f,0,{0}};
 bool ok=sceNetConnect(socket,&local,sizeof(local))>=0;
 static constexpr char request[]=
  "GET /api/preflight HTTP/1.1\r\n"
  "Host: 127.0.0.1:8877\r\n"
  "Connection: close\r\n\r\n";
 if(ok) {
  const int sent=sceNetSend(socket,request,sizeof(request)-1,0);
  ok=sent==static_cast<int>(sizeof(request)-1);
 }
 // Fixed maximum: the backend preflight contract cannot exceed 16 KiB.
 // Bounded stack allocation and no filesystem writes from the native UI.
 char response[18432]{};
 std::size_t received=0;
 while(ok && received<sizeof(response)-1) {
  const int got=sceNetRecv(socket,response+received,sizeof(response)-received-1,0);
  if(got<0){ok=false;break;}
  if(got==0)break;
  received+=static_cast<std::size_t>(got);
 }
 (void)sceNetSocketClose(socket);
 if(!ok||received==0||received>=sizeof(response)-1)return false;
 response[received]='\0';
 if(std::strncmp(response,"HTTP/1.1 200 ",13)!=0)return false;
 const char*json=std::strstr(response,"\r\n\r\n");
 if(!json)return false;
 json+=4;
 if(!std::strstr(json,"\"scope\":\"known-paths-only\""))return false;
 return scan_field(json,"\"accessible\":",&available)
   && scan_field(json,"\"denied\":",&denied)
   && scan_field(json,"\"absent\":",&absent);
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
  if(pressed&up)selected=(selected+row_count-1)%row_count;
  if(pressed&down)selected=(selected+1)%row_count;
  if(pressed&circle)selected=0;
  if((pressed&cross)&&selected==0){
   state=get_preflight()?Link::ok:Link::offline;
  }
 }
}
void scene(ps5::demo::Canvas&canvas) noexcept {
 poll_pad();
 canvas.clear(Color::background);
 canvas.rectangle(0,0,1920,12,Color::cyan);
 canvas.rectangle(72,110,1776,130,Color::panel);
 canvas.text(112,146,"CLEANMYPS5",9,Color::white);
 canvas.text(116,286,"STORAGE MAINTENANCE",4,Color::cyan);
 static constexpr const char*items[row_count]={
   "SMART ANALYSIS", "STORAGE ACCESS", "CLEANUP", "RETURN TO LIVE"
 };
 for(int i=0;i<row_count;i++){
  unsigned top=360+static_cast<unsigned>(i)*142;
  canvas.rectangle(100,top,1700,112,i==selected?Color::cyan:Color::panel);
  canvas.rectangle(109,top+9,1682,94,Color::background);
  canvas.text(145,top+29,items[i],5,i==selected?Color::cyan:Color::white);
 }
 canvas.rectangle(100,935,1700,100,Color::panel);
 if(state==Link::ok){
  char result[120]{};
  (void)std::snprintf(result,sizeof(result),"OPEN %u  DENIED %u  ABSENT %u",available,denied,absent);
  canvas.text(130,958,result,4,Color::white);
 }else if(state==Link::offline){
  canvas.text(130,958,"ENGINE OFFLINE OR NETWORK RESTRICTED",4,Color::yellow);
 }else{
  canvas.text(130,958,"X SCAN  UP DOWN NAVIGATE  CIRCLE BACK",4,Color::white);
 }
 if(selected==2)canvas.text(920,811,"NO DELETION UNTIL VERIFIED",3,Color::yellow);
 if(selected==1)canvas.text(920,811,"SANDBOX RIGHTS MAY DIFFER",3,Color::yellow);
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
