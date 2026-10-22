// CleanMyPS5 native dashboard (GPL-3.0-or-later).
// Real PS5 title: reads app-visible paths locally, without opening a browser.
// Advanced diagnostics require the separately privileged, read-only helper.
#include "demo_renderer.hpp"
#include "loopback_protocol.hpp"
#include "inventory.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <cerrno>
#include <sys/statvfs.h>
#include <atomic>
#include <pthread.h>
#include <cstdlib>

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
constexpr std::uint32_t up=0x10,down=0x40,cross=0x4000,circle=0x2000,square=0x8000;
constexpr int row_count=4;
enum class Screen { menu, analyze, access, cleanup, review, official };
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
  struct statvfs volume{};
  if(fstatvfs(fd,&volume)==0){
   const std::uint64_t block=volume.f_frsize?volume.f_frsize:volume.f_bsize;
   if(block && static_cast<std::uint64_t>(volume.f_bavail)<=UINT64_MAX/block){
    result.volume_known=true;
    result.available_bytes=static_cast<std::uint64_t>(volume.f_bavail)*block;
   }
  }
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
enum class ScanSource { none, native_title, helper };
Summary summary_status=Summary::unchecked;
ScanSource scan_source=ScanSource::none;
unsigned summary_files=0,summary_media=0,summary_cache=0,summary_logs=0;
unsigned summary_large=0,summary_old=0,summary_old_large=0;
std::uint64_t summary_bytes=0,summary_large_bytes=0;
bool summary_partial=false;
Cmp5MediaReview media_review{};
unsigned review_selection=0;
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
bool helper_is_compatible() noexcept {
 char response[1024]{};
 const char*json=nullptr;
 if(!fetch_local("/api/capabilities",response,sizeof(response),&json))return false;
 return cmp5_native::matches_expected_helper(json,std::strlen(json));
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
bool accept_overview(const char*json,ScanSource source) noexcept {
 if(!json||!std::strstr(json,"\"scope\":\"bounded-known-roots\"")||
    !std::strstr(json,"\"cleanupValidated\":false")||
    !std::strstr(json,"\"validatedForDeletion\":false"))return false;
 unsigned files=0,media=0,cache=0,logs=0,large=0,old=0,oldLarge=0;
 std::uint64_t bytes=0,largeBytes=0;
 if(!scan_field(json,"\"files\":",&files)||
    !scan_u64(json,"\"nominalBytes\":",&bytes)||
    !scan_field(json,"\"media\":",&media)||
    !scan_field(json,"\"cache\":",&cache)||
    !scan_field(json,"\"logs\":",&logs)||
    !scan_field(json,"\"largeMedia\":",&large)||
    !scan_field(json,"\"oldMedia\":",&old)||
    !scan_field(json,"\"oldLargeMedia\":",&oldLarge)||
    !scan_u64(json,"\"largeMediaNominalBytes\":",&largeBytes)||
    large>media||old>media||oldLarge>large||oldLarge>old)return false;
 summary_files=files;
 summary_media=media;
 summary_cache=cache;
 summary_logs=logs;
 summary_bytes=bytes;
 summary_large=large;
 summary_old=old;
 summary_old_large=oldLarge;
 summary_large_bytes=largeBytes;
 summary_partial=std::strstr(json,"\"partial\":true")!=nullptr;
 summary_status=Summary::ok;
 scan_source=source;
 return true;
}
void run_overview() noexcept {
 // The same limited scanner is linked inside eboot.bin. It has only
 // the permissions actually granted to the installed title's sandbox.
 // Never present inaccessible roots as empty or deleted.
 summary_status=Summary::unavailable;
 scan_source=ScanSource::none;
 // The title itself discovers media independently of the helper.
 // Its results may be partial because the sandbox cannot read all roots.
 std::size_t length=0;
 char*json=cmp5_overview_with_review("/",&length,&media_review);
 if(json){
  (void)accept_overview(json,ScanSource::native_title);
  std::free(json);
 }
 if(review_selection>=media_review.count)review_selection=0;
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
 engine=Link::offline;
 if(helper_is_compatible()&&get_preflight())engine=Link::ok;
 run_overview();
}
enum class Task : unsigned { none, environment, access, overview };
std::atomic<unsigned> task_state{0}; // 0=idle, 1=busy, 2=complete, 3=start failed
Task task_kind=Task::none;

void* background_task(void*) noexcept {
 switch(task_kind){
  case Task::environment: run_environment_check(); break;
  case Task::access: probe_local(); break;
  case Task::overview: run_overview(); break;
  default: break;
 }
 // Publish all task results to the video thread only after writes complete.
 task_state.store(2,std::memory_order_release);
 return nullptr;
}
bool begin_task(Task kind) noexcept {
 if(task_state.load(std::memory_order_acquire)==1)return false;
 task_kind=kind;
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
void poll_pad() noexcept {
 if(controller<0)return;
 RawPadSample samples[32]{};
 const int count=scePadRead(controller,samples,32);
 if(count<=0)return;
 for(int i=0;i<count;i++){
  const std::uint32_t buttons=samples[i].connected!=0?samples[i].buttons:0;
  const std::uint32_t pressed=buttons&~previous;
  previous=buttons;
  if(pressed&circle){screen=screen==Screen::review?Screen::cleanup:Screen::menu;continue;}
  if(task_busy())continue;
  if(screen==Screen::review){
   if(media_review.count){
    if(pressed&up)review_selection=(review_selection+media_review.count-1)%media_review.count;
    if(pressed&down)review_selection=(review_selection+1)%media_review.count;
   }
   continue;
  }
  if(screen==Screen::official){
   if(pressed&up)official_selection=(official_selection+7u)%8u;
   if(pressed&down)official_selection=(official_selection+1u)%8u;
   if(pressed&cross)official_checked[official_selection]=!official_checked[official_selection];
   continue;
  }
  if(screen==Screen::cleanup&&(pressed&square)){
   if(media_review.count){review_selection=0;screen=Screen::review;}
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
   "ANALYSE EN COURS    X OUVRIR UNE SECTION    HAUT / BAS":
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
 if(task_failed()){
  panel(canvas,366,"CONTROLE NON LANCE","ECHEC DE CREATION DU WORKER",Color::yellow);
  footer(canvas,"X RETENTER    CERCLE RETOUR");
  return;
 }
 if(engine==Link::ok){
  char result[112]{};
  (void)std::snprintf(result,sizeof(result),"ACCESSIBLES %u  REFUSES %u  ABSENTS %u",
                       available,denied,absent);
  panel(canvas,366,"DIAGNOSTIC DU MOTEUR",result,Color::cyan);
 }else{
  panel(canvas,366,"MOTEUR NON DISPONIBLE","ABSENT OU VERSION INCOMPATIBLE",Color::yellow);
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
    "BILAN NATIF DU TITRE":"BILAN DU MOTEUR",detail,Color::cyan);
  (void)std::snprintf(detail,sizeof(detail),"CAPTURES %u  CACHES %u  LOGS %u",
    summary_media,summary_cache,summary_logs);
  panel(canvas,813,"CATEGORIES VUES",detail);
 }else{
  panel(canvas,664,"BILAN DU STOCKAGE",
    summary_status==Summary::unchecked?"X POUR LANCER LE SCAN":"ANALYSE INDISPONIBLE",
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
 if(task_failed()){
  panel(canvas,366,"CONTROLE NON LANCE","WORKER INDISPONIBLE",Color::yellow);
  footer(canvas,"X RETENTER    CERCLE RETOUR");
  return;
 }
 for(std::size_t i=0;i<4;i++){
  const auto&probe=local_probe[i];
  const auto state=probe.status;
  char detail[100]{};
  if(state==LocalAccess::listable){
   if(probe.volume_known)
    (void)std::snprintf(detail,sizeof(detail),"%u VUES  %llu MIO DISPO%s",
      probe.entries,static_cast<unsigned long long>(probe.available_bytes/1048576ULL),
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
  panel(canvas,353+static_cast<unsigned>(i)*144,local_names[i],
    detail,state==LocalAccess::listable||state==LocalAccess::opened?Color::cyan:Color::yellow);
 }
 footer(canvas,"X VERIFIER A NOUVEAU    CERCLE RETOUR");
}
void show_cleanup(ps5::demo::Canvas&canvas) noexcept {
 if(task_busy()){
  panel(canvas,355,"BILAN DES CAPTURES","ANALYSE EN COURS",Color::cyan);
  panel(canvas,503,"LE SYSTEME RESTE RESPONSIF","CERCLE POUR RETOURNER");
  panel(canvas,799,"PROTECTION","LECTURE SEULE  AUCUNE SUPPRESSION",Color::yellow);
  footer(canvas,"ANALYSE EN COURS    CERCLE RETOUR");
  return;
 }
 if(task_failed()){
  panel(canvas,355,"ANALYSE NON LANCEE","WORKER INDISPONIBLE",Color::yellow);
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
  panel(canvas,651,"ESPACE A EXAMINER",result,Color::yellow);
 }else{
  panel(canvas,355,"CAPTURES VOLUMINEUSES","BILAN NON DISPONIBLE",Color::yellow);
  panel(canvas,503,"CAPTURES ANCIENNES","BILAN NON DISPONIBLE",Color::yellow);
  panel(canvas,651,"MOTEUR","DROITS INSUFFISANTS OU SCAN ECHOUE",Color::yellow);
 }
 panel(canvas,799,"PROTECTION","AUCUN FICHIER DECLARE SUPPRIMABLE",Color::yellow);
 footer(canvas,media_review.count?
  "CARRE VOIR CAPTURES  X REANALYSER  CERCLE RETOUR":
  "X ANALYSER  AUCUN EFFACEMENT  CERCLE RETOUR");
}
void show_review(ps5::demo::Canvas&canvas) noexcept {
 if(!media_review.count){
  panel(canvas,367,"CAPTURES NON VISIBLES","ACCES LIMITE OU AUCUNE CAPTURE",Color::yellow);
  footer(canvas,"CERCLE RETOUR  AUCUNE SUPPRESSION");
  return;
 }
 char title[100]{};
 (void)std::snprintf(title,sizeof(title),"%u ELEMENTS VUS  %s",
  media_review.media_scanned,media_review.partial?"SCAN PARTIEL":"SCANS LIMITES");
 canvas.text(112,302,title,3,Color::yellow);
 const unsigned page=review_selection/4u;
 for(unsigned slot=0;slot<4;slot++){
  const unsigned index=page*4u+slot;
  if(index>=media_review.count)break;
  const auto&item=media_review.entries[index];
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
  (void)std::snprintf(stats,sizeof(stats),"%s  %llu MIO",
    item.media_type==2u?"VIDEO":"IMAGE",
    static_cast<unsigned long long>(item.bytes/1048576ULL));
  canvas.text(1320,top+36,stats,3,Color::cyan);
 }
 footer(canvas,"HAUT BAS NAVIGUER  CERCLE RETOUR  LECTURE SEULE");
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
 footer(canvas,"X MARQUER REVU  HAUT BAS  CERCLE  AUCUNE GARANTIE PSN");
}
void scene(ps5::demo::Canvas&canvas) noexcept {
 poll_pad();
 const char*subtitle=screen==Screen::menu?"MAINTENANCE PS5":
   screen==Screen::analyze?"ANALYSE DE L'ENVIRONNEMENT":
   screen==Screen::access?"PERMISSIONS DU TITRE":
   screen==Screen::cleanup?"NETTOYAGE PROTEGE":
   screen==Screen::review?"CAPTURES A EXAMINER":"RETOUR AU SYSTEME OFFICIEL";
 header(canvas,subtitle);
 switch(screen){
  case Screen::menu:show_menu(canvas);break;
  case Screen::analyze:show_analyze(canvas);break;
  case Screen::access:show_access(canvas);break;
  case Screen::cleanup:show_cleanup(canvas);break;
  case Screen::review:show_review(canvas);break;
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
