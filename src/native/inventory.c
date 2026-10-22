#define _DEFAULT_SOURCE
#include "inventory.h"
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
#define ITEM_CAP 10000u
#define DEPTH_CAP 12
#define SCAN_MAX_NANOSECONDS 5000000000ULL
typedef struct {char *p;size_t len,cap;int fail;} Buffer;
typedef struct {dev_t dev; ino_t ino;} Identity;
typedef struct {const char *id;char path[PATH_CAP];const char *status;size_t items,visits;unsigned long long bytes;} Coverage;
typedef struct {Buffer b;size_t visits,items;int stopped,partial,rootPartial,summary_only;char warnings[32][256];size_t nw;uint64_t deadline;
 Identity seen[VISIT_CAP];size_t nseen,aliases,links,special,excluded;Coverage coverage[sizeof(cmp5_roots)/sizeof(*cmp5_roots)+16];size_t nc,rootStart;unsigned long long bytes;
 size_t media,logs,caches,residuals,other;
 size_t largeMedia,oldMedia,oldLargeMedia;
 unsigned long long largeMediaBytes;
 time_t scanEpoch;
 Cmp5MediaReview *review;} Scan;
static int listed(const char *name,const char *const *names,size_t count){
 for(size_t i=0;i<count;i++)if(!strcasecmp(name,names[i]))return 1;
 return 0;
}
static int private_name(const char *name,int directory){
 if(name[0]=='.')return 1;
 if(directory)return listed(name,cmp5_private_dirs,sizeof(cmp5_private_dirs)/sizeof(*cmp5_private_dirs));
 return listed(name,cmp5_private_files,sizeof(cmp5_private_files)/sizeof(*cmp5_private_files));
}
static int seen(Scan*s,const struct stat*st){
 for(size_t i=0;i<s->nseen;i++)if(s->seen[i].dev==st->st_dev&&s->seen[i].ino==st->st_ino){s->aliases++;return 1;}
 if(s->nseen>=VISIT_CAP){s->stopped=1;return 1;}
 s->seen[s->nseen++]=(Identity){st->st_dev,st->st_ino};return 0;
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
 const char*mark=strstr(path,c.media_type==2?"/video/":"/photo/");
 if(mark){
  mark+=7;
  if(!strncmp(mark,"NPXS",4)){
   const char*slash=strchr(mark,'/');
   if(slash)mark=slash+1;
  }
  size_t t=0;
  while(mark[t]&&mark[t]!='/'&&t<sizeof(c.title_id)-1){
   unsigned char v=(unsigned char)mark[t];
   if(v>='a'&&v<='z')c.title_id[t]=(char)(v-32);
   else if((v>='A'&&v<='Z')||(v>='0'&&v<='9'))c.title_id[t]=(char)v;
   else break;
   t++;
  }
  if(mark[t]!='/')c.title_id[0]=0;
 }
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
static void walk(Scan*s,int fd,const char*path,const char*kind,int depth){
 if(s->stopped||s->b.fail||s->rootPartial){close(fd);return;}
 // Keep the directory descriptor supplied by the caller for anchored
 // openat/fstatat. The native-title toolchain does not export dirfd().
 // fdopendir takes ownership and closedir closes the same descriptor.
 const int anchored_fd=fd;
 DIR*d=fdopendir(fd);
 if(!d){s->rootPartial=1;warn(s,"Acces refuse",path);close(fd);return;}
 struct dirent*e;
 while((e=readdir(d))!=NULL){
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
  if(!join(child,sizeof(child),path,e->d_name)){warn(s,"Nom trop long",path);continue;}
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
  }else if(S_ISREG(st.st_mode))file(s,child,&st,kind);
  if(s->stopped||s->b.fail||s->rootPartial)break;
 }
 closedir(d);
}
static void root_scan(Scan*s,const char*root,const char*id,const char*rel,const char*kind){
 Coverage*c=&s->coverage[s->nc++];c->id=id;c->status="not-scanned";
 snprintf(c->path,sizeof(c->path),"/%s",rel);
 if(s->stopped){s->partial=1;return;}
 char copy[PATH_CAP],path[PATH_CAP];
 if(strlen(rel)>=sizeof(copy)){c->status="partial";s->partial=1;return;}
 snprintf(path,sizeof(path),"/%s",rel);memcpy(copy,rel,strlen(rel)+1);
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
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
char *cmp5_report(const char*root,size_t*len){
 if(len)*len=0;
 if(!root||!len)return NULL;
 struct stat st;if(lstat(root,&st)||!S_ISDIR(st.st_mode))return NULL;
 Scan*s=(Scan*)calloc(1,sizeof(Scan));if(!s)return NULL;
 uint64_t now=monotonic_ns();
 if(now && now<UINT64_MAX-SCAN_MAX_NANOSECONDS)s->deadline=now+SCAN_MAX_NANOSECONDS;
 add(&s->b,"{\"schemaVersion\":1,\"source\":\"CleanMyPS5 (read-only)\",\"ruleVersion\":");quote(&s->b,cmp5_rule_version);
 add(&s->b,",\"mode\":\"metadata-only\",\"cleanupValidated\":false,\"items\":[");
 for(size_t i=0;i<sizeof(cmp5_roots)/sizeof(*cmp5_roots);i++)
  root_scan(s,root,cmp5_roots[i].id,cmp5_roots[i].path,cmp5_roots[i].kind);
 for(int slot=0;slot<8;slot++){
  char path[80];snprintf(path,sizeof(path),"mnt/usb%d/homebrew",slot);root_scan(s,root,"usb-homebrew",path,"application");
  snprintf(path,sizeof(path),"mnt/ext%d/homebrew",slot);root_scan(s,root,"extended-homebrew",path,"application");
 }
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
char *cmp5_overview_with_review(const char *root,size_t *len,Cmp5MediaReview *review){
 if(len)*len=0;
 if(!root||!len)return NULL;
 struct stat st;
 if(lstat(root,&st)||!S_ISDIR(st.st_mode))return NULL;
 Scan*s=(Scan*)calloc(1,sizeof(*s));if(!s)return NULL;
 s->summary_only=1;
 s->review=review;
 if(review)memset(review,0,sizeof(*review));
 s->scanEpoch=time(NULL);
 uint64_t now=monotonic_ns();
 if(now && now<UINT64_MAX-SCAN_MAX_NANOSECONDS)s->deadline=now+SCAN_MAX_NANOSECONDS;
 for(size_t i=0;i<sizeof(cmp5_roots)/sizeof(*cmp5_roots);i++)
  root_scan(s,root,cmp5_roots[i].id,cmp5_roots[i].path,cmp5_roots[i].kind);
 for(int slot=0;slot<8;slot++){
  char path[80];
  snprintf(path,sizeof(path),"mnt/usb%d/homebrew",slot);
  root_scan(s,root,"usb-homebrew",path,"application");
  snprintf(path,sizeof(path),"mnt/ext%d/homebrew",slot);
  root_scan(s,root,"extended-homebrew",path,"application");
 }
 size_t complete=0,part=0,absent=0,unavailable=0,other=0;
 for(size_t i=0;i<s->nc;i++){
  const char*status=s->coverage[i].status;
  if(!strcmp(status,"complete"))complete++;
  else if(!strcmp(status,"partial"))part++;
  else if(!strcmp(status,"absent"))absent++;
  else if(!strcmp(status,"unavailable"))unavailable++;
  else other++;
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
   "\"review\":{\"largeMedia\":%zu,\"oldMedia\":%zu,\"oldLargeMedia\":%zu,"
   "\"largeMediaNominalBytes\":%llu,\"validatedForDeletion\":false,"
   "\"minLargeBytes\":52428800,\"oldDays\":90},"
   "\"limits\":{\"maxVisited\":30000,\"maxItems\":10000,"
   "\"maxEntriesPerRoot\":2500},"
   "\"note\":\"Nominal bytes only; never an estimate of deletable data\"}",
   s->items,s->visits,s->bytes,(s->stopped||s->partial)?"true":"false",
   s->media,s->logs,s->caches,s->residuals,s->other,
   complete,part,absent,unavailable,other,
   s->largeMedia,s->oldMedia,s->oldLargeMedia,s->largeMediaBytes);
  if(n<0||(size_t)n>=1024){free(out);out=NULL;}
  else *len=(size_t)n;
 }
 if(review){
  review->partial=(s->stopped||s->partial||review->media_scanned>review->count)?1u:0u;
  qsort(review->entries,review->count,sizeof(review->entries[0]),cmp_media_largest);
 }
 free(s);
 return out;
}

char *cmp5_overview(const char *root,size_t *len){
 return cmp5_overview_with_review(root,len,NULL);
}
