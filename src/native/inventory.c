#define _DEFAULT_SOURCE
#include "inventory.h"
#include "media_snapshot.h"
#include "scan_rules.h"
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#define PATH_CAP 2048
#define OUTPUT_CAP (8u*1024u*1024u)
#define VISIT_CAP 30000u
#define SEEN_HASH_CAP 65536u /* power of two, <= 46% load */
#define ITEM_CAP 10000u
#define DEPTH_CAP 12
#define SCAN_MAX_NANOSECONDS 5000000000ULL
typedef struct {char *p;size_t len,cap;int fail;} Buffer;
typedef struct {dev_t dev; ino_t ino;} Identity;
typedef struct {char title_id[24];char game_name[56];} GalleryTitle;
typedef struct {const char *id;char path[PATH_CAP];const char *status;size_t items,visits;unsigned long long bytes;} Coverage;
typedef struct {Buffer b;size_t visits,items;int stopped,partial,rootPartial,summary_only;char warnings[32][256];size_t nw;uint64_t deadline;
 Identity seen[VISIT_CAP];uint32_t seenSlots[SEEN_HASH_CAP];size_t nseen,aliases,links,special,excluded;Coverage coverage[sizeof(cmp5_roots)/sizeof(*cmp5_roots)+16];size_t nc,rootStart;unsigned long long bytes;
 size_t media,logs,caches,residuals,other;
 size_t largeMedia,oldMedia,oldLargeMedia;
 unsigned long long largeMediaBytes;
 time_t scanEpoch;
 Cmp5MediaReview *review;
 GalleryTitle gallery_titles[128];
 unsigned gallery_title_count,sidecar_reads;
 Cmp5CancelCheck should_cancel;void *cancel_context;} Scan;
static int listed(const char *name,const char *const *names,size_t count){
 for(size_t i=0;i<count;i++)if(!strcasecmp(name,names[i]))return 1;
 return 0;
}
static int private_name(const char *name,int directory){
 if(name[0]=='.')return 1;
 if(directory)return listed(name,cmp5_private_dirs,sizeof(cmp5_private_dirs)/sizeof(*cmp5_private_dirs));
 return listed(name,cmp5_private_files,sizeof(cmp5_private_files)/sizeof(*cmp5_private_files));
}
static uint64_t seen_hash(dev_t dev,ino_t ino){
 /* Hash, not a replacement for exact device/inode equality. */
 uint64_t h=(uint64_t)dev;
 h^=(uint64_t)ino+UINT64_C(0x9e3779b97f4a7c15)+(h<<6)+(h>>2);
 h^=h>>30;h*=UINT64_C(0xbf58476d1ce4e5b9);
 h^=h>>27;h*=UINT64_C(0x94d049bb133111eb);
 return h^(h>>31);
}
static int seen(Scan*s,const struct stat*st){
 size_t slot=(size_t)(seen_hash(st->st_dev,st->st_ino)&(SEEN_HASH_CAP-1u));
 for(size_t probes=0;probes<SEEN_HASH_CAP;probes++){
  const uint32_t index=s->seenSlots[slot];
  if(index==0){
   if(s->nseen>=VISIT_CAP){s->stopped=1;return 1;}
   s->seen[s->nseen]=(Identity){st->st_dev,st->st_ino};
   s->seenSlots[slot]=(uint32_t)(++s->nseen);
   return 0;
  }
  const Identity*known=&s->seen[index-1u];
  if(known->dev==st->st_dev&&known->ino==st->st_ino){
   s->aliases++;return 1;
  }
  slot=(slot+1u)&(SEEN_HASH_CAP-1u);
 }
 s->stopped=1;return 1; /* Hash saturation fails closed. */
}
static uint64_t monotonic_ns(void){
 struct timespec t;
 if(clock_gettime(CLOCK_MONOTONIC,&t)!=0||t.tv_sec<0)return 0;
 return (uint64_t)t.tv_sec*1000000000ULL+(uint64_t)t.tv_nsec;
}
static void put(Buffer*b,const char*s,size_t n){
 if(b->fail||n>=OUTPUT_CAP-b->len){b->fail=1;return;}
 size_t want=b->len+n+1;
 if(want>b->cap){size_t cap=b->cap?b->cap:4096;while(cap<want && cap<OUTPUT_CAP)cap*=2;if(cap>OUTPUT_CAP)cap=OUTPUT_CAP;
  char*t=(char*)realloc(b->p,cap);if(!t||cap<want){b->fail=1;return;}b->p=t;b->cap=cap;}
 memcpy(b->p+b->len,s,n);b->len+=n;b->p[b->len]=0;
}
static void add(Buffer*b,const char*s){put(b,s,strlen(s));}
static void quote(Buffer*b,const char*s){
 add(b,"\"");
 for(const unsigned char*p=(const unsigned char*)s;*p;p++){
  const unsigned char c=*p;
  if(c=='"'||c=='\\'){char q[2]={'\\',(char)c};put(b,q,2);}
  else if(c<32){char q[7];snprintf(q,sizeof(q),"\\u%04x",(unsigned)c);put(b,q,6);}
  else put(b,(const char*)p,1);
 }
 add(b,"\"");
}
static int join(char*out,size_t cap,const char*parent,const char*child){
 const size_t n=strlen(parent);const int r=snprintf(out,cap,"%s%s%s",parent,n&&parent[n-1]=='/'?"":"/",child);
 return r>=0&&(size_t)r<cap;
}
static int ext(const char*path,const char*extension){
 size_t n=strlen(path),k=strlen(extension);if(n<k)return 0;
 for(size_t i=0;i<k;i++)if(tolower((unsigned char)path[n-k+i])!=extension[i])return 0;
 return 1;
}
static void warn(Scan*s,const char*message,const char*path){
 s->partial=1;s->rootPartial=1;
 if(s->nw>=32)return;
 snprintf(s->warnings[s->nw++],256,"%s : %.170s",message,path);
}
/* PS5 13.60 gallery sidecars (.dat) store the game display name at 0x44.
 * The name is untrusted local metadata: read at most 160 bytes through
 * the already anchored parent fd, filter to Canvas-safe ASCII, strip the
 * trailing _YYYYMMDDHHMMSS suffix, never expose paths or log its contents.
 */
static void gallery_title_id(const char*path,const char*kind,char out[24]){
 out[0]=0;
 const char*needle=!strcmp(kind,"video")?"/video/":"/photo/";
 const char*mark=strstr(path,needle);
 if(!mark)return;
 mark+=7;
 if(!strncmp(mark,"NPXS",4)){
  const char*slash=strchr(mark,'/');
  if(!slash)return;
  mark=slash+1;
 }
 size_t n=0;
 while(mark[n]&&mark[n]!='/'&&n<23u){
  const unsigned char v=(unsigned char)mark[n];
  if(v>='a'&&v<='z')out[n]=(char)(v-32);
  else if((v>='A'&&v<='Z')||(v>='0'&&v<='9'))out[n]=(char)v;
  else {out[0]=0;return;}
  n++;
 }
 if(!n||mark[n]!='/'){out[0]=0;return;}
 out[n]=0;
}
/* Reject mid-read sidecar edits and leaf replacement. Whole-second mtime
 * alone misses in-place modifications during the same clock second. */
static int same_sidecar_stat(const struct stat*a,const struct stat*b){
#if defined(__APPLE__)
 const int nanos=a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec&&
   a->st_ctimespec.tv_nsec==b->st_ctimespec.tv_nsec;
#else
 const int nanos=a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
   a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
#endif
 return nanos&&S_ISREG(a->st_mode)&&S_ISREG(b->st_mode)&&
   a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
   a->st_size==b->st_size&&a->st_mtime==b->st_mtime&&
   a->st_ctime==b->st_ctime&&a->st_mode==b->st_mode&&
   a->st_nlink==1&&b->st_nlink==1;
}
static void read_gallery_sidecar(Scan*s,int parentfd,const char*basename,
 const char*path,const char*kind,const struct stat*expected){
 if(!s->review||s->sidecar_reads>=128||!ext(basename,".dat"))return;
 char id[24];
 gallery_title_id(path,kind,id);
 if(!id[0])return;
 for(unsigned i=0;i<s->gallery_title_count;i++)
  if(!strcmp(s->gallery_titles[i].title_id,id))return;
 if(s->gallery_title_count>=128)return;
 s->sidecar_reads++;
 const int fd=openat(parentfd,basename,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 if(fd<0)return;
 struct stat st;
 if(fstat(fd,&st)!=0||st.st_size<0x45||st.st_size>65536||
    !same_sidecar_stat(&st,expected)){
  close(fd);s->rootPartial=1;s->partial=1;return;
 }
 unsigned char raw[161]={0};
 if(lseek(fd,0x44,SEEK_SET)<0){
  close(fd);s->rootPartial=1;s->partial=1;return;
 }
 ssize_t got;
 do{got=read(fd,raw,160);}while(got<0&&errno==EINTR);
 struct stat after,named;
 const int stable=got>0&&fstat(fd,&after)==0&&
   fstatat(parentfd,basename,&named,AT_SYMLINK_NOFOLLOW)==0&&
   same_sidecar_stat(&st,&after)&&same_sidecar_stat(&st,&named);
 close(fd);
 if(!stable){s->rootPartial=1;s->partial=1;return;}
 if(raw[0]==0)return;
 // The last fourteen digits after an underscore identify the capture time.
 size_t len=0;
 while(len<(size_t)got&&raw[len]&&len<160)len++;
 if(len>15&&raw[len-15]=='_'){
  int timestamp=1;
  for(size_t k=len-14;k<len;k++)if(raw[k]<'0'||raw[k]>'9')timestamp=0;
  if(timestamp)len-=15;
 }
 char safe[56]={0};size_t n=0;
 for(size_t i=0;i<len&&n<sizeof(safe)-1;i++){
  const unsigned char ch=raw[i];
  if((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
     (ch>='0'&&ch<='9')||ch==' '||ch=='-'||ch=='\''||
     ch=='.'||ch=='&'||ch==':')safe[n++]=(char)ch;
  else if(n&&safe[n-1]!=' ')safe[n++]=' ';
 }
 while(n&&safe[n-1]==' ')n--;
 if(!n)return;
 safe[n]=0;
 GalleryTitle*t=&s->gallery_titles[s->gallery_title_count++];
 memcpy(t->title_id,id,strlen(id)+1);
 memcpy(t->game_name,safe,n+1);
}

/* Keep just a fixed top-N of media metadata in the current scan.
 * No files are opened or modified; no directory paths reach the UI.
 */
static void capture_review(Scan*s,const char*path,const struct stat*info,const char*kind){
 Cmp5MediaReview*r=s->review;
 if(!r)return;
 r->media_scanned++;
 Cmp5MediaCandidate c={0};
 c.bytes=(uint64_t)info->st_size;
 c.modified_at=info->st_mtime>0?(int64_t)info->st_mtime:0;
 c.media_type=!strcmp(kind,"video")?2u:1u;
 /* Only real gallery captures are eligible for preview; never app data. */
 const char*prefix=c.media_type==1u?"/user/av_contents/photo/":"/user/av_contents/video/";
 const size_t path_length=strlen(path);
 if(strncmp(path,prefix,strlen(prefix))==0 &&
    path_length<sizeof(c.preview_path) && info->st_size>0){
  memcpy(c.preview_path,path,path_length+1u);
  cmp5_snapshot_capture(&c,info);
 }
 const char *base=strrchr(path,'/');
 base=base?base+1:path;
 size_t n=0;
 for(const unsigned char*p=(const unsigned char*)base;*p&&n<sizeof(c.label)-1;p++){
  if(*p>='a'&&*p<='z')c.label[n++]=(char)(*p-32);
  else if((*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9'))c.label[n++]=(char)*p;
  else if(n&&c.label[n-1]!=' ')c.label[n++]=' ';
 }
 while(n&&c.label[n-1]==' ')--n;
 c.label[n]=0;
 if(!n)memcpy(c.label,"CAPTURE",8);
 gallery_title_id(path,kind,c.title_id);
 if(r->count<CMP5_REVIEW_CAP)r->entries[r->count++]=c;
}
static int cmp_media_largest(const void*a,const void*b){
 const Cmp5MediaCandidate*x=(const Cmp5MediaCandidate*)a;
 const Cmp5MediaCandidate*y=(const Cmp5MediaCandidate*)b;
 if(x->bytes!=y->bytes)return x->bytes>y->bytes?-1:1;
 if(x->modified_at!=y->modified_at)return x->modified_at>y->modified_at?-1:1;
 int title=strcmp(x->title_id,y->title_id);
 return title?title:strcmp(x->label,y->label);
}

static void file(Scan*s,const char*path,const struct stat*info,const char*kind){
 if(s->stopped||s->b.fail||info->st_size<0)return;
 const off_t size=info->st_size;
 if((uint64_t)size>9007199254740991ULL){s->rootPartial=1;return;}
 /* A sum of individually safe JSON integers can exceed uint64_t.
  * Abort rather than wrapping storage totals or showing false free space. */
 if((uint64_t)size>UINT64_MAX-s->bytes){
  s->stopped=1;
  warn(s,"Volume nominal non representable",path);
  return;
 }
 const char *category="other",*risk="protected",*reason="Donnee inconnue : aucune regle de suppression validee";
 if((!strcmp(kind,"photo")&&(ext(path,".jpg")||ext(path,".jpeg")||ext(path,".png")||ext(path,".jxr")))||
    (!strcmp(kind,"video")&&(ext(path,".mp4")||ext(path,".webm")))){
  category="media";risk="review";reason="Capture personnelle : conserver les associations galerie, variantes et miniatures";
 }else if(!strcmp(kind,"associated")||!strcmp(kind,"photo")||!strcmp(kind,"video")){
  category="other";reason="Fichier associe aux captures : index, miniature ou variante a conserver";
 }else if(!strcmp(kind,"system-log")){
  category="logs";reason="Journal ou base systeme active : purge non validee, WAL/SHM et historique proteges";
 }else if(!strcmp(kind,"system-cache")){
  category="cache";reason="Cache systeme : base et fichiers associes proteges, purge non validee";
 }else if(!strcmp(kind,"runtime")){
  reason="Etat temporaire ou communication active : ne pas supprimer";
 }else if(ext(path,".log")||strstr(path,"/logs/")){
  category="logs";reason="Journal potentiel d'application : motif de nom uniquement, conservation obligatoire";
 }else if(strstr(path,"/cache/")||strstr(path,"/caches/")){
  category="cache";reason="Cache potentiel d'application : dependances inconnues, conservation obligatoire";
 }else if(ext(path,".elf")||ext(path,".bin")||ext(path,".sprx")||ext(path,".prx")||
          ext(path,".ini")||ext(path,".json")||ext(path,".db")){
  category="residuals";reason="Programme, ressource ou configuration : aucune preuve de residu";
 }
 if(!s->summary_only && s->b.len>OUTPUT_CAP-65536u){s->stopped=1;warn(s,"Limite de taille du rapport",path);return;}
 if(s->items>=ITEM_CAP){s->stopped=1;warn(s,"Nombre limite de fichiers",path);return;}
 if(s->summary_only){
  if(!strcmp(category,"media")){
   s->media++;
   capture_review(s,path,info,kind);
   const int large=(uint64_t)size>=50ULL*1024ULL*1024ULL;
   const int old=s->scanEpoch>90*86400 && info->st_mtime>0 &&
     info->st_mtime<=s->scanEpoch-90*86400;
   if(large){s->largeMedia++;s->largeMediaBytes+=(unsigned long long)size;}
   if(old)s->oldMedia++;
   if(old&&large)s->oldLargeMedia++;
  }
  else if(!strcmp(category,"logs"))s->logs++;
  else if(!strcmp(category,"cache"))s->caches++;
  else if(!strcmp(category,"residuals"))s->residuals++;
  else s->other++;
  s->items++;s->bytes+=(unsigned long long)size;
  return;
 }
 char id[24],n[32];snprintf(id,sizeof(id),"%zu",s->items+1);
 snprintf(n,sizeof(n),"%llu",(unsigned long long)size);
 if(s->items)add(&s->b,",");
 add(&s->b,"{\"id\":");quote(&s->b,id);
 add(&s->b,",\"path\":");quote(&s->b,path);
 add(&s->b,",\"bytes\":");add(&s->b,n);
 add(&s->b,",\"category\":");quote(&s->b,category);
 add(&s->b,",\"risk\":");quote(&s->b,risk);
 add(&s->b,",\"reason\":");quote(&s->b,reason);
 if(strcmp(category,"media")==0){
   add(&s->b,",\"previewPath\":");quote(&s->b,path);
 }
 if(strcmp(category,"media")==0){
   const char*mark=strstr(path,!strcmp(kind,"photo")?"/photo/":"/video/");
   if(mark){
     mark+=7; /* both /photo/ and /video/ have seven bytes */
     if(!strncmp(mark,"NPXS",4)){const char *next=strchr(mark,'/');mark=next?next+1:mark;}
     size_t n=0;while(mark[n]&&mark[n]!='/'&&n<24&&(isalnum((unsigned char)mark[n])||mark[n]=='_'))n++;
     if(n&&mark[n]=='/'){char title[25];memcpy(title,mark,n);title[n]=0;add(&s->b,",\"titleId\":");quote(&s->b,title);}
   }
 }

 char epoch[32];snprintf(epoch,sizeof(epoch),"%lld",(long long)(info->st_mtime>0?info->st_mtime:0));
 add(&s->b,",\"modifiedAt\":");add(&s->b,epoch);
 add(&s->b,"}");
 s->items++;s->bytes+=(unsigned long long)size;
}
// Enumerate via an anchored directory fd. A symlink swap in an ancestor
// cannot cause traversal into an unrelated homebrew, title or OS tree.
static int cancellation_requested(Scan*s){
 if(s->should_cancel&&s->should_cancel(s->cancel_context)){
  s->stopped=1;s->partial=1;return 1;
 }
 return 0;
}
static void walk(Scan*s,int fd,const char*path,const char*kind,int depth){
 if(s->stopped||s->b.fail||s->rootPartial||cancellation_requested(s)){close(fd);return;}
 // Keep the directory descriptor supplied by the caller for anchored
 // openat/fstatat. The native-title toolchain does not export dirfd().
 // fdopendir takes ownership and closedir closes the same descriptor.
 const int anchored_fd=fd;
 DIR*d=fdopendir(fd);
 if(!d){s->rootPartial=1;warn(s,"Acces refuse",path);close(fd);return;}
 struct dirent*e;
 for(;;){
  /* readdir() returns NULL both on end-of-directory and on I/O failure.
   * Reset errno BEFORE each call because fstatat/openat also modify it.
   * A read failure must never produce a false "complete" root. */
  errno=0;
  e=readdir(d);
  if(!e){
   if(errno!=0){
    s->rootPartial=1;
    warn(s,"Erreur de lecture du dossier",path);
   }
   break;
  }
  if(cancellation_requested(s)){s->rootPartial=1;break;}
  if(strcmp(e->d_name,".")==0||strcmp(e->d_name,"..")==0)continue;
  if(s->visits-s->rootStart>=2500){s->rootPartial=1;warn(s,"Limite locale de parcours",path);break;}
  if(++s->visits>VISIT_CAP){s->stopped=1;warn(s,"Limite de parcours atteinte",path);break;}
  if(s->deadline && (s->visits&63u)==0u){
   uint64_t now=monotonic_ns();
   if(now && now>s->deadline){
    s->stopped=1;warn(s,"Analyse partielle : limite de 5 secondes atteinte",path);break;
   }
  }
  char child[PATH_CAP];
  if(!join(child,sizeof(child),path,e->d_name)){
   s->rootPartial=1;
   warn(s,"Chemin trop long : scan incomplet",path);
   break;
  }
  struct stat st;
  if(fstatat(anchored_fd,e->d_name,&st,AT_SYMLINK_NOFOLLOW)!=0){
   s->rootPartial=1;warn(s,"Entree inaccessible",child);continue;
  }
  if(S_ISLNK(st.st_mode)){s->links++;continue;}
  if(!strcmp(kind,"application")&&private_name(e->d_name,S_ISDIR(st.st_mode))){s->excluded++;continue;}
  if(!S_ISDIR(st.st_mode)&&!S_ISREG(st.st_mode)){s->special++;continue;}
  if(seen(s,&st))continue;
  if(S_ISDIR(st.st_mode)){
   if(!strcmp(kind,"runtime")){s->excluded++;continue;}
   if(depth<DEPTH_CAP){
    int next=openat(anchored_fd,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(next<0){s->rootPartial=1;warn(s,"Dossier inaccessible",child);}
    else walk(s,next,child,kind,depth+1);
   }else {s->rootPartial=1;warn(s,"Profondeur maximale atteinte",child);}
  }else if(S_ISREG(st.st_mode)){
   if(s->summary_only&&(!strcmp(kind,"photo")||!strcmp(kind,"video"))&&
      ext(e->d_name,".dat"))
    read_gallery_sidecar(s,anchored_fd,e->d_name,child,kind,&st);
   file(s,child,&st,kind);
  }
  if(s->stopped||s->b.fail||s->rootPartial)break;
 }
 if(closedir(d)!=0){
  s->rootPartial=1;
  warn(s,"Erreur fermeture du dossier",path);
 }
}
/* Bind the entire inventory to one directory descriptor rather than
 * reopening an attacker-replaceable pathname between scan categories. */
static int open_scan_root(const char*root){
 if(!root)return -1;
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return -1;
 struct stat st;
 if(fstat(fd,&st)!=0||!S_ISDIR(st.st_mode)){close(fd);return -1;}
 return fd;
}
static void root_scan(Scan*s,int rootfd,const char*id,const char*rel,const char*kind){
 Coverage*c=&s->coverage[s->nc++];c->id=id;c->status="not-scanned";
 snprintf(c->path,sizeof(c->path),"/%s",rel);
 if(s->stopped||cancellation_requested(s)){s->partial=1;return;}
 char copy[PATH_CAP],path[PATH_CAP];
 if(strlen(rel)>=sizeof(copy)){c->status="partial";s->partial=1;return;}
 snprintf(path,sizeof(path),"/%s",rel);memcpy(copy,rel,strlen(rel)+1);
 int fd=dup(rootfd);
 if(fd<0){c->status="unavailable";s->partial=1;return;}
 char*save=NULL;
 for(char*part=strtok_r(copy,"/",&save);part;part=strtok_r(NULL,"/",&save)){
  int child=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  if(child<0){
   int err=errno;close(fd);c->status=err==ENOENT?"absent":"unavailable";
   if(err!=ENOENT)s->partial=1;
   return;
  }
  close(fd);fd=child;
 }
 struct stat st;
 if(fstat(fd,&st)){close(fd);c->status="unavailable";s->partial=1;return;}
 if(seen(s,&st)){close(fd);c->status="alias-skipped";return;}
 size_t before=s->items;unsigned long long bytes=s->bytes;
 s->rootStart=s->visits;s->rootPartial=0;
 walk(s,fd,path,kind,0);
 c->items=s->items-before;c->visits=s->visits-s->rootStart;c->bytes=s->bytes-bytes;
 c->status=(s->stopped||s->rootPartial)?"partial":"complete";
 if(s->stopped||s->rootPartial)s->partial=1;
}
/* No configured scanner root was traversable: an all-zero inventory
 * is not evidence that a PS5 installation contains zero relevant files.
 * Distinguish the absence of observations from a complete empty gallery. */
static int has_completed_root(const Scan*s){
 for(size_t i=0;i<s->nc;i++)
  if(!strcmp(s->coverage[i].status,"complete"))return 1;
 return 0;
}
/* A readable application or USB root cannot validate the Gallery.
 * Neither can a lone photo OR video directory. Both categories must have
 * actually completed a bounded scan; 'absent' cannot mean 'empty and safe'.
 * This is only a conservative completeness signal, not delete approval. */
static int has_completed_gallery_root(const Scan*s){
 int photo=0,video=0;
 for(size_t i=0;i<s->nc;i++){
  const Coverage*c=&s->coverage[i];
  if(strcmp(c->status,"complete"))continue;
  if(!strcmp(c->id,"captures-photo"))photo=1;
  if(!strcmp(c->id,"captures-video"))video=1;
 }
 return photo&&video;
}
char *cmp5_report(const char*root,size_t*len){
 if(len)*len=0;
 if(!root||!len)return NULL;
 int rootfd=open_scan_root(root);
 if(rootfd<0)return NULL;
 Scan*s=(Scan*)calloc(1,sizeof(Scan));
 if(!s){close(rootfd);return NULL;}
 uint64_t now=monotonic_ns();
 if(now && now<UINT64_MAX-SCAN_MAX_NANOSECONDS)s->deadline=now+SCAN_MAX_NANOSECONDS;
 add(&s->b,"{\"schemaVersion\":1,\"source\":\"CleanMyPS5 (read-only)\",\"ruleVersion\":");quote(&s->b,cmp5_rule_version);
 add(&s->b,",\"mode\":\"metadata-only\",\"cleanupValidated\":false,\"items\":[");
 for(size_t i=0;i<sizeof(cmp5_roots)/sizeof(*cmp5_roots);i++)
  root_scan(s,rootfd,cmp5_roots[i].id,cmp5_roots[i].path,cmp5_roots[i].kind);
 for(int slot=0;slot<8;slot++){
  char path[80];snprintf(path,sizeof(path),"mnt/usb%d/homebrew",slot);root_scan(s,rootfd,"usb-homebrew",path,"application");
  snprintf(path,sizeof(path),"mnt/ext%d/homebrew",slot);root_scan(s,rootfd,"extended-homebrew",path,"application");
 }
 close(rootfd);
 if(!has_completed_root(s)||!has_completed_gallery_root(s))s->partial=1;
 add(&s->b,"],\"coverage\":[");
 for(size_t i=0;i<s->nc;i++){
  Coverage*c=&s->coverage[i];if(i)add(&s->b,",");
  add(&s->b,"{\"id\":");quote(&s->b,c->id);add(&s->b,",\"path\":");quote(&s->b,c->path);add(&s->b,",\"status\":");quote(&s->b,c->status);
  char nums[180];snprintf(nums,sizeof(nums),",\"items\":%zu,\"visited\":%zu,\"nominalBytes\":%llu}",c->items,c->visits,c->bytes);add(&s->b,nums);
 }
 add(&s->b,"],\"warnings\":[");
 for(size_t i=0;i<s->nw;i++){if(i)add(&s->b,",");quote(&s->b,s->warnings[i]);}
 char stats[700];
 snprintf(stats,sizeof(stats),"],\"stats\":{\"visited\":%zu,\"items\":%zu,\"partial\":%s,\"aliasesSkipped\":%zu,\"symlinksSkipped\":%zu,\"specialFilesSkipped\":%zu,\"privateEntriesExcluded\":%zu},\"limits\":{\"maxVisited\":30000,\"maxItems\":10000,\"maxEntriesPerRoot\":2500,\"maxDepth\":12,\"softDurationSeconds\":5}}",
   s->visits,s->items,(s->stopped||s->partial)?"true":"false",s->aliases,s->links,s->special,s->excluded);
 add(&s->b,stats);
 char*output=s->b.p;
 if(s->b.fail){free(output);output=NULL;}else *len=s->b.len;
 free(s);return output;
}

/* Small, privacy-safe native summary. Uses the SAME bounded traversal and
 * classification as the full report, but never serializes a file name.
 * Sizes are nominal; no space is implied to be safely recoverable.
 */
char *cmp5_overview_with_review_cancel(const char *root,size_t *len,Cmp5MediaReview *review,Cmp5CancelCheck should_cancel,void *context){
 if(len)*len=0;
 if(!root||!len)return NULL;
 int rootfd=open_scan_root(root);
 if(rootfd<0)return NULL;
 Scan*s=(Scan*)calloc(1,sizeof(*s));
 if(!s){close(rootfd);return NULL;}
 s->summary_only=1;
 s->review=review;
 s->should_cancel=should_cancel;
 s->cancel_context=context;
 if(review)memset(review,0,sizeof(*review));
 s->scanEpoch=time(NULL);
 uint64_t now=monotonic_ns();
 if(now && now<UINT64_MAX-SCAN_MAX_NANOSECONDS)s->deadline=now+SCAN_MAX_NANOSECONDS;
 for(size_t i=0;i<sizeof(cmp5_roots)/sizeof(*cmp5_roots);i++)
  root_scan(s,rootfd,cmp5_roots[i].id,cmp5_roots[i].path,cmp5_roots[i].kind);
 for(int slot=0;slot<8;slot++){
  char path[80];
  snprintf(path,sizeof(path),"mnt/usb%d/homebrew",slot);
  root_scan(s,rootfd,"usb-homebrew",path,"application");
  snprintf(path,sizeof(path),"mnt/ext%d/homebrew",slot);
  root_scan(s,rootfd,"extended-homebrew",path,"application");
 }
 close(rootfd);
 (void)cancellation_requested(s);
 if(!has_completed_root(s)||!has_completed_gallery_root(s))s->partial=1;
 size_t complete=0,part=0,absent=0,unavailable=0,other=0;
 for(size_t i=0;i<s->nc;i++){
  const char*status=s->coverage[i].status;
  if(!strcmp(status,"complete"))complete++;
  else if(!strcmp(status,"partial"))part++;
  else if(!strcmp(status,"absent"))absent++;
  else if(!strcmp(status,"unavailable"))unavailable++;
  else other++;
 }
 /* Separate capture-root status: successful /data access cannot imply
  * visibility of both the native photo and video Gallery namespaces. */
 const char*photo="not-scanned",*video="not-scanned";
 for(size_t i=0;i<s->nc;i++){
  if(!strcmp(s->coverage[i].id,"captures-photo"))photo=s->coverage[i].status;
  if(!strcmp(s->coverage[i].id,"captures-video"))video=s->coverage[i].status;
 }
 char *out=(char*)calloc(1,1024);
 if(out){
  int n=snprintf(out,1024,
   "{\"schemaVersion\":1,\"mode\":\"metadata-only\",\"cleanupValidated\":false,"
   "\"scope\":\"bounded-known-roots\",\"files\":%zu,\"visited\":%zu,"
   "\"nominalBytes\":%llu,\"partial\":%s,"
   "\"categories\":{\"media\":%zu,\"logs\":%zu,\"cache\":%zu,"
   "\"residuals\":%zu,\"other\":%zu},"
   "\"coverage\":{\"complete\":%zu,\"partial\":%zu,\"absent\":%zu,"
   "\"unavailable\":%zu,\"other\":%zu},"
   "\"galleryCoverage\":{\"photo\":\"%s\",\"video\":\"%s\"},"
   "\"review\":{\"largeMedia\":%zu,\"oldMedia\":%zu,\"oldLargeMedia\":%zu,"
   "\"largeMediaNominalBytes\":%llu,\"validatedForDeletion\":false,"
   "\"minLargeBytes\":52428800,\"oldDays\":90},"
   "\"limits\":{\"maxVisited\":30000,\"maxItems\":10000,"
   "\"maxEntriesPerRoot\":2500},"
   "\"note\":\"Nominal bytes only; never an estimate of deletable data\"}",
   s->items,s->visits,s->bytes,(s->stopped||s->partial)?"true":"false",
   s->media,s->logs,s->caches,s->residuals,s->other,
   complete,part,absent,unavailable,other,photo,video,
   s->largeMedia,s->oldMedia,s->oldLargeMedia,s->largeMediaBytes);
  if(n<0||(size_t)n>=1024){free(out);out=NULL;}
  else *len=(size_t)n;
 }
 if(review){
  review->partial=(s->stopped||s->partial||review->media_scanned>review->count)?1u:0u;
  for(unsigned i=0;i<review->count;i++){
   Cmp5MediaCandidate*item=&review->entries[i];
   for(unsigned j=0;j<s->gallery_title_count;j++){
    if(!strcmp(item->title_id,s->gallery_titles[j].title_id)){
     memcpy(item->game_name,s->gallery_titles[j].game_name,
       strlen(s->gallery_titles[j].game_name)+1);
     break;
    }
   }
  }
  qsort(review->entries,review->count,sizeof(review->entries[0]),cmp_media_largest);
 }
 free(s);
 return out;
}

char *cmp5_overview_with_review(const char *root,size_t *len,Cmp5MediaReview *review){
 return cmp5_overview_with_review_cancel(root,len,review,NULL,NULL);
}
char *cmp5_overview(const char *root,size_t *len){
 return cmp5_overview_with_review_cancel(root,len,NULL,NULL,NULL);
}
