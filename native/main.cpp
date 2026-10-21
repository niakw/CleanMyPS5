// CleanMyPS5 native dashboard (GPL-3.0-or-later).
// Real PS5 title: reads app-visible paths locally, without opening a browser.
// Advanced diagnostics require the separately privileged, read-only helper.
#include "demo_renderer.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>

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
constexpr std::uint32_t up=0x10,down=0x40,cross=0x4000,circle=0x2000;
constexpr int row_count=4;
enum class Screen { menu, analyze, access, cleanup, official };
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
enum class LocalAccess { unchecked, unavailable, opened, listable, enumeration_denied };
LocalAccess local_access[4]{};

void probe_local() noexcept {
 // A directory may be openable yet not enumerable under the title sandbox.
 // Do not equate open(O_RDONLY) with directory listing or deletion rights.
 for(std::size_t i=0;i<4;i++){
  const int fd=open(local_paths[i],0);
  if(fd<0){local_access[i]=LocalAccess::unavailable;continue;}
  (void)close(fd);
  if(i==0){local_access[i]=LocalAccess::opened;continue;}
  DIR*dir=opendir(local_paths[i]);
  if(dir){local_access[i]=LocalAccess::listable;(void)closedir(dir);}
  else local_access[i]=LocalAccess::enumeration_denied;
 }
 local_checked=true;
}
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
 (void)sceNetSetsockopt(socket,0xffff,0x1105,&timeout_us,sizeof(timeout_us));
 (void)sceNetSetsockopt(socket,0xffff,0x1106,&timeout_us,sizeof(timeout_us));
 const NetSockaddrIn local{16,2,static_cast<std::uint16_t>((8877u<<8)|(8877u>>8)),0x0100007f,0,{0}};
 bool ok=sceNetConnect(socket,&local,sizeof(local))>=0;
 static constexpr char request[]=
  "GET /api/preflight HTTP/1.1\r\n"
  "Host: 127.0.0.1:8877\r\n"
  "Connection: close\r\n\r\n";
 if(ok){
  const int sent=sceNetSend(socket,request,sizeof(request)-1,0);
  ok=sent==static_cast<int>(sizeof(request)-1);
 }
 char response[18432]{};
 std::size_t received=0;
 while(ok && received<sizeof(response)-1){
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
 unsigned a=0,d=0,b=0;
 if(!scan_field(json,"\"accessible\":",&a)||
    !scan_field(json,"\"denied\":",&d)||
    !scan_field(json,"\"absent\":",&b))return false;
 available=a;denied=d;absent=b;
 return true;
}
void run_environment_check() noexcept {
 probe_local();
 engine=get_preflight()?Link::ok:Link::offline;
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
  if(pressed&circle){screen=Screen::menu;continue;}
  if(screen==Screen::menu){
   if(pressed&up)selected=(selected+row_count-1)%row_count;
   if(pressed&down)selected=(selected+1)%row_count;
   if(pressed&cross){
    screen=selected==0?Screen::analyze:selected==1?Screen::access:
      selected==2?Screen::cleanup:Screen::official;
    if(screen==Screen::analyze)run_environment_check();
    if(screen==Screen::access)probe_local();
   }
  }else if(pressed&cross){
   if(screen==Screen::analyze)run_environment_check();
   if(screen==Screen::access)probe_local();
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
 footer(canvas,"X OUVRIR    HAUT / BAS NAVIGUER");
}
void show_analyze(ps5::demo::Canvas&canvas) noexcept {
 if(engine==Link::ok){
  char result[112]{};
  (void)std::snprintf(result,sizeof(result),"ACCESSIBLES %u  REFUSES %u  ABSENTS %u",
                       available,denied,absent);
  panel(canvas,366,"DIAGNOSTIC DU MOTEUR",result,Color::cyan);
 }else{
  panel(canvas,366,"MOTEUR NON DISPONIBLE","AUCUN RAPPORT GLOBAL",Color::yellow);
 }
 panel(canvas,515,"ANALYSE LOCALE",local_checked?
  "VERIFICATION DANS LE SANDBOX":"NON EXECUTEE");
 panel(canvas,664,"LIMITES DE COUVERTURE",
  "ACCES APP DIFFERENT DU PAYLOAD",Color::yellow);
 panel(canvas,813,"SECURITE","AUCUNE SUPPRESSION EFFECTUEE");
 footer(canvas,"X RECONTROLER    CERCLE RETOUR");
}
void show_access(ps5::demo::Canvas&canvas) noexcept {
 for(std::size_t i=0;i<4;i++){
  const auto state=local_access[i];
  const char*detail=state==LocalAccess::unchecked?"NON TESTE":
    state==LocalAccess::opened?"FICHIER OUVERT":
    state==LocalAccess::listable?"DOSSIER LISTABLE":
    state==LocalAccess::enumeration_denied?"OUVERT MAIS LISTE BLOQUEE":
    "INACCESSIBLE OU ABSENT";
  panel(canvas,353+static_cast<unsigned>(i)*144,local_names[i],
    detail,state==LocalAccess::listable||state==LocalAccess::opened?Color::cyan:Color::yellow);
 }
 footer(canvas,"X VERIFIER A NOUVEAU    CERCLE RETOUR");
}
void show_cleanup(ps5::demo::Canvas&canvas) noexcept {
 panel(canvas,355,"NETTOYAGE DES CAPTURES","NON ACTIVE",Color::yellow);
 panel(canvas,503,"NETTOYAGE DES CACHES","NON ACTIVE",Color::yellow);
 panel(canvas,651,"SUPPRESSION DES DOUBLONS","NON ACTIVE",Color::yellow);
 panel(canvas,799,"PRECONDITION","ACCES, SAUVEGARDE, VALIDATION");
 footer(canvas,"PAS D'EFFACEMENT SANS CONTROLE   CERCLE RETOUR");
}
void show_official(ps5::demo::Canvas&canvas) noexcept {
 panel(canvas,355,"REVOIR LES HOMEBREWS","VERIFIER MANUELLEMENT");
 panel(canvas,503,"SAUVEGARDER SES DONNEES","AVANT UNE MISE A JOUR");
 panel(canvas,651,"PARAMETRES OFFICIELS","UTILISER LA CONSOLE");
 panel(canvas,799,"SERVICES EN LIGNE","AUCUNE GARANTIE ANTI-BAN",Color::yellow);
 footer(canvas,"AUCUNE MODIFICATION DU SYSTEME   CERCLE RETOUR");
}
void scene(ps5::demo::Canvas&canvas) noexcept {
 poll_pad();
 const char*subtitle=screen==Screen::menu?"MAINTENANCE PS5":
   screen==Screen::analyze?"ANALYSE DE L'ENVIRONNEMENT":
   screen==Screen::access?"PERMISSIONS DU TITRE":
   screen==Screen::cleanup?"NETTOYAGE PROTEGE":"RETOUR AU SYSTEME OFFICIEL";
 header(canvas,subtitle);
 switch(screen){
  case Screen::menu:show_menu(canvas);break;
  case Screen::analyze:show_analyze(canvas);break;
  case Screen::access:show_access(canvas);break;
  case Screen::cleanup:show_cleanup(canvas);break;
  case Screen::official:show_official(canvas);break;
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
