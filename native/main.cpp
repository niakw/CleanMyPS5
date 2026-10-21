// CleanMyPS5 native dashboard (GPL-3.0-or-later).
// Real PS5 title: reads app-visible paths locally, without opening a browser.
// Advanced diagnostics require the separately privileged, read-only helper.
#include "demo_renderer.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <cerrno>

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
enum class LocalAccess { unchecked, unavailable, missing, denied, opened, listable, enumeration_denied };
struct LocalProbe {
 LocalAccess status=LocalAccess::unchecked;
 unsigned entries=0;
 bool truncated=false;
 bool io_error=false;
};
LocalProbe local_probe[4]{};
bool local_checked=false;
constexpr unsigned max_entries_per_root=256;

LocalAccess open_error(int value) noexcept {
 if(value==ENOENT)return LocalAccess::missing;
 if(value==EACCES||value==EPERM)return LocalAccess::denied;
 return LocalAccess::unavailable;
}
void probe_local() noexcept {
 // Only enumerate immediate directory entries visible to the installed title.
 // Never follow links, walk into child directories, open file contents or write.
 // The cap is intentional: numbers are sampled, NOT a full storage inventory.
 for(std::size_t i=0;i<4;i++){
  LocalProbe result{};
  const int fd=open(local_paths[i],0);
  if(fd<0){result.status=open_error(errno);local_probe[i]=result;continue;}
  (void)close(fd);
  if(i==0){result.status=LocalAccess::opened;local_probe[i]=result;continue;}
  errno=0;
  DIR*dir=opendir(local_paths[i]);
  if(!dir){result.status=open_error(errno)==LocalAccess::missing?
     LocalAccess::missing:open_error(errno)==LocalAccess::denied?
     LocalAccess::denied:LocalAccess::enumeration_denied;local_probe[i]=result;continue;}
  result.status=LocalAccess::listable;
  errno=0;
  for(;;){
   struct dirent*entry=readdir(dir);
   if(!entry){
    if(errno!=0)result.io_error=true;
    break;
   }
   if(std::strcmp(entry->d_name,".")==0||std::strcmp(entry->d_name,"..")==0)continue;
   if(result.entries==max_entries_per_root){result.truncated=true;break;}
   ++result.entries;
   errno=0;
  }
  (void)closedir(dir);
  local_probe[i]=result;
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
enum class Summary { unchecked, ok, unavailable };
Summary summary_status=Summary::unchecked;
unsigned summary_files=0,summary_media=0,summary_cache=0,summary_logs=0;
std::uint64_t summary_bytes=0;
bool summary_partial=false;

// Native UI uses only fixed paths and a closed loopback listener.
// Bounded receive buffers; the backend never receives a write command.
bool fetch_local(const char*route,char*response,std::size_t capacity,
                 const char**json) noexcept {
 if(!route||!response||capacity<256||!json)return false;
 if(std::strcmp(route,"/api/preflight")&&std::strcmp(route,"/api/overview"))return false;
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
 if(std::strncmp(response,"HTTP/1.1 200 ",13)!=0)return false;
 const char*body=std::strstr(response,"\r\n\r\n");
 if(!body)return false;
 *json=body+4;
 return true;
}
bool scan_u64(const char*text,const char*name,std::uint64_t*out) noexcept {
 const char*start=std::strstr(text,name);
 if(!start)return false;
 start+=std::strlen(name);
 if(*start<'0'||*start>'9')return false;
 std::uint64_t n=0;
 while(*start>='0'&&*start<='9'){
  const unsigned digit=static_cast<unsigned>(*start-'0');
  if(n>(UINT64_MAX-digit)/10)return false;
  n=n*10+digit;
  ++start;
 }
 *out=n;
 return true;
}
bool get_preflight() noexcept {
 char response[18432]{};
 const char*json=nullptr;
 if(!fetch_local("/api/preflight",response,sizeof(response),&json))return false;
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
 summary_status=Summary::unchecked;
}
void run_overview() noexcept {
 summary_status=Summary::unavailable;
 // May take several seconds on large drives. Only execute on explicit X action.
 char response[2048]{};
 const char*json=nullptr;
 if(!fetch_local("/api/overview",response,sizeof(response),&json))return;
 if(!std::strstr(json,"\"scope\":\"bounded-known-roots\"")||
    !std::strstr(json,"\"cleanupValidated\":false"))return;
 unsigned files=0,media=0,cache=0,logs=0;
 std::uint64_t bytes=0;
 if(!scan_field(json,"\"files\":",&files)||
    !scan_u64(json,"\"nominalBytes\":",&bytes)||
    !scan_field(json,"\"media\":",&media)||
    !scan_field(json,"\"cache\":",&cache)||
    !scan_field(json,"\"logs\":",&logs))return;
 summary_files=files;
 summary_media=media;
 summary_cache=cache;
 summary_logs=logs;
 summary_bytes=bytes;
 summary_partial=std::strstr(json,"\"partial\":true")!=nullptr;
 summary_status=Summary::ok;
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
   if(screen==Screen::analyze)run_overview();
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
  panel(canvas,664,"BILAN DU HELPER",detail,Color::cyan);
  (void)std::snprintf(detail,sizeof(detail),"CAPTURES %u  CACHES %u  LOGS %u",
    summary_media,summary_cache,summary_logs);
  panel(canvas,813,"CATEGORIES VUES",detail);
 }else{
  panel(canvas,664,"BILAN DU HELPER",
    summary_status==Summary::unchecked?"X POUR LANCER LE SCAN":"INDISPONIBLE",
    Color::yellow);
  panel(canvas,813,"PROTECTION","AUCUNE SUPPRESSION EFFECTUEE");
 }
 footer(canvas,"X ANALYSE APPROFONDIE    CERCLE RETOUR");
}
void show_access(ps5::demo::Canvas&canvas) noexcept {
 for(std::size_t i=0;i<4;i++){
  const auto&probe=local_probe[i];
  const auto state=probe.status;
  char detail[100]{};
  if(state==LocalAccess::listable){
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
