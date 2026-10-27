#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "homebrew.h"
#include "scan_rules.h"
#include <strings.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define NAME_CAP 2048
#define JSON_CAP (512u*1024u)
#define TOTAL_CAP 5000u
#define ITEMS_CAP 256u
#define HOMEBREW_NAMES_CAP 128u
#define HOMEBREW_NAME_LEN 64u
#define DEPTH_CAP 5
#define TIME_CAP_NS 3000000000ULL
typedef struct {char*data;size_t len;int bad;} Writer;
typedef struct {size_t visited,items;int partial;uint64_t deadline;Writer out;
 unsigned data_root_state,installed_root_state;
 size_t homebrew_name_count;
 char homebrew_names[HOMEBREW_NAMES_CAP][HOMEBREW_NAME_LEN];
 Cmp5HomebrewStats *stats;Cmp5CancelCheck cancel;void *context;
 time_t cache_wall_now;} Audit;
typedef struct {time_t now;int recent;int unknown;} CacheChange;
/* mtime is only a signal of recent writes; its age never proves idle. */
static void note_cache_mtime(CacheChange*change,time_t modified){
 if(!change)return;
 if(change->now<=0||modified<=0||difftime(modified,change->now)>0.0)
  change->unknown=1;
 else if(difftime(change->now,modified)<=86400.0)change->recent=1;
}
/* A missing optional plugin/USB folder is normal. Missing /data or
 * /user/app is different: a zero inventory must not imply complete access. */
static unsigned root_state_from_errno(int error){
 if(error==ENOENT)return 2u;
 if(error==EACCES||error==EPERM)return 3u;
 return 4u;
}
static const char *root_state_name(unsigned state){
 switch(state){
 case 1u:return "readable";
 case 2u:return "absent";
 case 3u:return "denied";
 case 4u:return "error";
 default:return "not-scanned";
 }
}
static uint64_t now_ns(void){
 struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0)return 0;
 return (uint64_t)t.tv_sec*1000000000ULL+(uint64_t)t.tv_nsec;
}
static void add(Writer*w,const char*text){
 size_t n=strlen(text);
 if(w->bad||n>=JSON_CAP-w->len){w->bad=1;return;}
 memcpy(w->data+w->len,text,n+1);w->len+=n;
}
static void quote(Writer*w,const char*text){
 add(w,"\"");
 for(const unsigned char*p=(const unsigned char*)text;*p;p++){
  char b[8];
  if(*p=='"'||*p=='\\'){b[0]='\\';b[1]=(char)*p;b[2]=0;add(w,b);}
  else if(*p<32){snprintf(b,sizeof(b),"\\u%04x",(unsigned)*p);add(w,b);}
  else{b[0]=(char)*p;b[1]=0;add(w,b);}
 }
 add(w,"\"");
}
static int join(char*out,size_t cap,const char*parent,const char*name){
 size_t n=strlen(parent);int m=snprintf(out,cap,"%s%s%s",parent,n&&parent[n-1]=='/'?"":"/",name);
 return m>=0&&(size_t)m<cap;
}
static int over(Audit*a){
 if(a->cancel && a->cancel(a->context)){a->partial=1;return 1;}
 if(a->visited>=TOTAL_CAP){a->partial=1;return 1;}
 uint64_t n=now_ns();
 if(a->deadline&&n&&n>a->deadline){a->partial=1;return 1;}
 return 0;
}
static void count_tree(Audit*a,int fd,int depth,uint64_t*size,size_t*files,
                       int*partial,CacheChange*change){
 if(over(a)){*partial=1;if(change)change->unknown=1;close(fd);return;}
 const int anchored_fd=fd;
 struct stat parent={0};
 if(fstat(fd,&parent)!=0||!S_ISDIR(parent.st_mode)){
  close(fd);*partial=1;if(change)change->unknown=1;return;
 }
 DIR*d=fdopendir(fd);
 if(!d){close(fd);*partial=1;if(change)change->unknown=1;return;}
 struct dirent*e;
 for(;;){
  /* A NULL entry means EOF or an I/O failure. Metadata operations on
   * earlier children can modify errno, so clear it before each read. */
  errno=0;
  e=readdir(d);
  if(!e){if(errno!=0){*partial=1;if(change)change->unknown=1;}break;}
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
  a->visited++;
  if(over(a)){*partial=1;if(change)change->unknown=1;break;}
  struct stat st;
  if(fstatat(anchored_fd,e->d_name,&st,AT_SYMLINK_NOFOLLOW)){
   *partial=1;if(change)change->unknown=1;continue;
  }
  if(S_ISLNK(st.st_mode)){if(change)change->unknown=1;continue;}
  int excluded=e->d_name[0]=='.';
  const char *const *names=S_ISDIR(st.st_mode)?cmp5_private_dirs:cmp5_private_files;
  size_t count=S_ISDIR(st.st_mode)?sizeof(cmp5_private_dirs)/sizeof(*cmp5_private_dirs):sizeof(cmp5_private_files)/sizeof(*cmp5_private_files);
  for(size_t i=0;i<count;i++)if(!strcasecmp(e->d_name,names[i]))excluded=1;
  if(excluded){if(change)change->unknown=1;continue;}
  if(change)note_cache_mtime(change,st.st_mtime);
  if(S_ISREG(st.st_mode)){
   (*files)++;
   if(st.st_size>=0){
    uint64_t v=(uint64_t)st.st_size;
    if(v>UINT64_MAX-*size){*partial=1;continue;}
    *size+=v;
   }
  }else if(S_ISDIR(st.st_mode)){
   if(depth>=DEPTH_CAP||st.st_dev!=parent.st_dev){
    *partial=1;if(change)change->unknown=1;continue;
   }
   int child=openat(anchored_fd,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
   if(child<0){*partial=1;if(change)change->unknown=1;continue;}
   struct stat opened={0};
   if(fstat(child,&opened)!=0||!S_ISDIR(opened.st_mode)||
      opened.st_dev!=st.st_dev||opened.st_ino!=st.st_ino){
    close(child);*partial=1;if(change)change->unknown=1;continue;
   }
   const uint64_t before_bytes=*size;
   const size_t before_files=*files;
   count_tree(a,child,depth+1,size,files,partial,change);
   /* A rename/swap invalidates that subtree's attribution. Discard
    * its stale bytes and file count, even though the parent scan
    * necessarily stays partial and cannot prove inactivity. */
   struct stat after={0};
   if(fstatat(anchored_fd,e->d_name,&after,AT_SYMLINK_NOFOLLOW)!=0||
      !S_ISDIR(after.st_mode)||after.st_dev!=st.st_dev||
      after.st_ino!=st.st_ino){
    *size=before_bytes;*files=before_files;
    *partial=1;if(change)change->unknown=1;
   }
  }else{
   *partial=1;if(change)change->unknown=1;
  }
  /* A corrupt sibling is not a global hard stop. Continue to count
   * readable files within the fixed visit/time/depth budgets. */
 }
 if(closedir(d)!=0){*partial=1;if(change)change->unknown=1;}
}
static int open_dir(const char*root,const char*relative){
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return -1;
 char parts[NAME_CAP];
 if(strlen(relative)>=sizeof(parts)){close(fd);return -1;}
 memcpy(parts,relative,strlen(relative)+1);
 char*save=NULL;
 for(char*name=strtok_r(parts,"/",&save);name;name=strtok_r(NULL,"/",&save)){
  int child=openat(fd,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  close(fd);fd=child;if(fd<0)return -1;
 }
 return fd;
}
/* Retain only four top-level app/plugin/installation observations.
 * Never store full paths in the native app summary. Keep a deterministic
 * rank for equal sizes; do not treat nominal bytes as reclaimable. */
static void record_top(Audit*a,const char*basename,const char*kind,
                       uint64_t bytes,size_t files,int partial){
 Cmp5HomebrewStats*stats=a->stats;
 if(!stats)return;
 Cmp5HomebrewTop item={0};
 item.nominal_bytes=bytes;
 item.files=files>UINT32_MAX?UINT32_MAX:(uint32_t)files;
 item.partial=partial?1u:0u;
 item.category=!strcmp(kind,"homebrew")?1u:
   !strcmp(kind,"plugin")?2u:!strcmp(kind,"app-data")?3u:4u;
 size_t len=0;
 for(const unsigned char*p=(const unsigned char*)basename;*p&&len<sizeof(item.label)-1u;p++){
  const unsigned char ch=*p;
  if((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
     (ch>='0'&&ch<='9')||ch=='-'||ch=='_'||ch=='.')
   item.label[len++]=(char)ch;
  else if(ch==' '&&len&&item.label[len-1]!=' ')
   item.label[len++]=' ';
 }
 while(len&&item.label[len-1]==' ')len--;
 if(!len){memcpy(item.label,"ELEMENT",8);len=7;}
 item.label[len]=0;
 unsigned where=0;
 for(;where<stats->top_count;where++){
  const Cmp5HomebrewTop*previous=&stats->top[where];
  if(item.nominal_bytes>previous->nominal_bytes)break;
  if(item.nominal_bytes==previous->nominal_bytes){
   if(item.category<previous->category)break;
   if(item.category==previous->category&&
      strcmp(item.label,previous->label)<0)break;
  }
 }
 if(where>=CMP5_HOMEBREW_TOP_CAP)return;
 if(stats->top_count<CMP5_HOMEBREW_TOP_CAP)stats->top_count++;
 for(unsigned i=stats->top_count-1u;i>where;i--)
  stats->top[i]=stats->top[i-1u];
 stats->top[where]=item;
}
/* Observe cache-like app-owned names WITHOUT declaring them disposable.
 * Only direct children under /data/<app> are considered; paths and cache
 * filenames are not retained in native Cmp5HomebrewStats. */
/* Metadata-only scanners must not quietly accept a directory swapped
 * between its name lookup, open and final lookup. This proves only the
 * directory identity at sampled points, NOT cache ownership or quiescence. */
static int same_cache_dir(const struct stat*a,const struct stat*b){
 return S_ISDIR(a->st_mode)&&S_ISDIR(b->st_mode)&&
        a->st_dev==b->st_dev&&a->st_ino==b->st_ino;
}
/* Basename correlation is only a hint, never proven provider control.
 * A non-symlink executable is required to distinguish a homebrew folder
 * from random data, but says nothing about the safety of its caches. */
static void remember_homebrew_name(Audit*a,const char*name,int fd){
 if(!a||!a->stats||!name||fd<0)return;
 const size_t length=strlen(name);
 if(!length||length>=HOMEBREW_NAME_LEN){
  a->stats->homebrew_name_index_partial=1u;return;
 }
 for(size_t i=0;i<length;i++){
  const unsigned char c=(unsigned char)name[i];
  if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
       (c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))
   return;
 }
 struct stat app={0},exe={0};
 if(fstat(fd,&app)!=0||!S_ISDIR(app.st_mode))return;
 const char*const executables[]={"eboot.elf","eboot.bin"};
 int found=0;
 for(size_t i=0;i<sizeof(executables)/sizeof(*executables);i++){
  if(fstatat(fd,executables[i],&exe,AT_SYMLINK_NOFOLLOW)==0&&
     S_ISREG(exe.st_mode)&&exe.st_size>0&&exe.st_dev==app.st_dev){
   found=1;break;
  }
 }
 if(!found)return;
 for(size_t i=0;i<a->homebrew_name_count;i++)
  if(strcmp(a->homebrew_names[i],name)==0)return;
 if(a->homebrew_name_count==HOMEBREW_NAMES_CAP){
  a->stats->homebrew_name_index_partial=1u;
  a->partial=1;return;
 }
 memcpy(a->homebrew_names[a->homebrew_name_count++],name,length+1u);
}
static int homebrew_name_observed(const Audit*a,const char*name){
 if(!a||!name)return 0;
 for(size_t i=0;i<a->homebrew_name_count;i++)
  if(strcmp(a->homebrew_names[i],name)==0)return 1;
 return 0;
}
static int cache_like_name(const char*name){
 return name&&(!strcasecmp(name,"cache")||!strcasecmp(name,".cache")||
               !strcasecmp(name,"tmp")||!strcasecmp(name,"temp"));
}
static void scan_cache_subdirs(Audit*a,int app_dir,const char*app_name){
 if(!a||!a->stats||app_dir<0)return;
 const int weak_match=homebrew_name_observed(a,app_name);
 /* dup() SHARES a directory offset with app_dir. Enumerating via a
  * duplicate would exhaust the parent's iterator before count_tree(),
  * silently undercounting ALL app bytes. Open a fresh description
  * anchored to this already verified directory instead. */
 const int iter=openat(app_dir,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(iter<0){a->stats->cache_like_partial=1u;a->partial=1;return;}
 DIR*d=fdopendir(iter);
 if(!d){close(iter);a->stats->cache_like_partial=1u;a->partial=1;return;}
 for(;;){
  if(over(a)){a->stats->cache_like_partial=1u;break;}
  errno=0;
  struct dirent*e=readdir(d);
  if(!e){
   if(errno){a->stats->cache_like_partial=1u;a->partial=1;}
   break;
  }
  if(!cache_like_name(e->d_name))continue;
  a->visited++;
  if(over(a)){a->stats->cache_like_partial=1u;break;}
  struct stat st={0};
  if(fstatat(app_dir,e->d_name,&st,AT_SYMLINK_NOFOLLOW)!=0){
   a->stats->cache_like_partial=1u;a->partial=1;continue;
  }
  /* A symlink or regular file called cache/temp is not a cache folder.
   * A mounted subvolume is also deliberately outside this one-app scope. */
  if(!S_ISDIR(st.st_mode))continue;
  struct stat parent={0};
  if(fstat(app_dir,&parent)!=0||!S_ISDIR(parent.st_mode)||
     st.st_dev!=parent.st_dev){
   a->stats->cache_like_partial=1u;a->partial=1;continue;
  }
  const int child=openat(app_dir,e->d_name,
                         O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  if(child<0){a->stats->cache_like_partial=1u;a->partial=1;continue;}
  struct stat opened={0};
  if(fstat(child,&opened)!=0||!same_cache_dir(&st,&opened)){
   close(child);a->stats->cache_like_partial=1u;a->partial=1;continue;
  }
  uint64_t bytes=0;size_t files=0;int partial=0;
  CacheChange change={0};
  change.now=a->cache_wall_now;
  note_cache_mtime(&change,st.st_mtime);
  count_tree(a,child,0,&bytes,&files,&partial,&change); /* owns child fd */
  struct stat after={0};
  if(fstatat(app_dir,e->d_name,&after,AT_SYMLINK_NOFOLLOW)!=0||
     !same_cache_dir(&st,&after)){
   a->stats->cache_like_partial=1u;a->partial=1;continue;
  }
  if(a->stats->cache_like_folders==UINT32_MAX||
     files>UINT32_MAX||
     a->stats->cache_like_files>UINT32_MAX-(uint32_t)files||
     a->stats->cache_like_nominal_bytes>UINT64_MAX-bytes){
   a->stats->cache_like_partial=1u;a->partial=1;
  }else{
   a->stats->cache_like_folders++;
   if(weak_match)a->stats->cache_like_name_matched++;
   else a->stats->cache_like_name_unmatched++;
   a->stats->cache_like_files+=(uint32_t)files;
   a->stats->cache_like_nominal_bytes+=bytes;
   if(partial||change.unknown)a->stats->cache_mtime_unknown++;
   else if(change.recent)a->stats->cache_mtime_recent++;
   else a->stats->cache_mtime_no_recent++;
  }
  if(partial){a->stats->cache_like_partial=1u;a->partial=1;}
 }
 if(closedir(d)!=0){a->stats->cache_like_partial=1u;a->partial=1;}
}
static void audit_root(Audit*a,const char*root,const char*relative,const char*kind){
 unsigned*critical=NULL;
 if(!strcmp(relative,"data"))critical=&a->data_root_state;
 else if(!strcmp(relative,"user/app"))critical=&a->installed_root_state;
 int fd=open_dir(root,relative);
 if(fd<0){
  const int error=errno;
  if(critical)*critical=root_state_from_errno(error);
  if(error!=ENOENT)a->partial=1;
  return;
 }
 const int anchored_fd=fd;
 DIR*d=fdopendir(fd);
 if(!d){
  if(critical)*critical=4u;
  close(fd);a->partial=1;return;
 }
 if(critical)*critical=1u;
 struct dirent*e;
 for(;;){
  errno=0;
  e=readdir(d);
  if(!e){if(errno!=0)a->partial=1;break;}
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
  /* Nonoverlapping accounting: /data/homebrew and etaHEN/plugins are
   * already listed separately. Do not count their containers a second
   * time as generic application data. Keep unrelated etaHEN files. */
  if(!strcmp(kind,"app-data")){
   if(!strcmp(relative,"data")&&
      (!strcasecmp(e->d_name,"homebrew")||
       !strcasecmp(e->d_name,"etaHEN")))continue;
   if(!strcmp(relative,"data/etaHEN")&&
      !strcasecmp(e->d_name,"plugins"))continue;
  }
  a->visited++;
  if(over(a)){a->partial=1;break;}
  struct stat st;
  if(fstatat(anchored_fd,e->d_name,&st,AT_SYMLINK_NOFOLLOW)){a->partial=1;continue;}
  if(S_ISLNK(st.st_mode)||(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode)))continue;
  int excluded=e->d_name[0]=='.';
  const char *const *names=S_ISDIR(st.st_mode)?cmp5_private_dirs:cmp5_private_files;
  size_t count=S_ISDIR(st.st_mode)?sizeof(cmp5_private_dirs)/sizeof(*cmp5_private_dirs):sizeof(cmp5_private_files)/sizeof(*cmp5_private_files);
  for(size_t i=0;i<count;i++)if(!strcasecmp(e->d_name,names[i]))excluded=1;
  if(excluded)continue;
  if(a->items>=ITEMS_CAP){a->partial=1;break;}
  char relative_path[NAME_CAP];
  if(!join(relative_path,sizeof(relative_path),relative,e->d_name)){a->partial=1;continue;}
  uint64_t bytes=0;size_t files=0;int partial=0;
  if(S_ISREG(st.st_mode)){if(st.st_size>=0)bytes=(uint64_t)st.st_size;files=1;}
  else{
   int child=openat(anchored_fd,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
   if(child<0)partial=1;
   else{
    struct stat opened={0};
    if(fstat(child,&opened)!=0||!same_cache_dir(&st,&opened)){
     close(child);partial=1;
    }else{
     const int app_data=!strcmp(kind,"app-data")&&
                        !strcmp(relative,"data");
     const uint32_t before_folders=a->stats->cache_like_folders;
     const uint32_t before_matched=a->stats->cache_like_name_matched;
     const uint32_t before_unmatched=a->stats->cache_like_name_unmatched;
     const uint32_t before_files=a->stats->cache_like_files;
     const uint64_t before_bytes=a->stats->cache_like_nominal_bytes;
     const uint32_t before_recent=a->stats->cache_mtime_recent;
     const uint32_t before_unknown=a->stats->cache_mtime_unknown;
     const uint32_t before_no_recent=a->stats->cache_mtime_no_recent;
     const size_t before_names=a->homebrew_name_count;
     if(app_data)scan_cache_subdirs(a,child,e->d_name);
     if(!strcmp(kind,"homebrew")&&!strcmp(relative,"data/homebrew"))
      remember_homebrew_name(a,e->d_name,child);
     count_tree(a,child,0,&bytes,&files,&partial,NULL);
     struct stat after={0};
     if(fstatat(anchored_fd,e->d_name,&after,AT_SYMLINK_NOFOLLOW)!=0||
        !same_cache_dir(&st,&after)){
      /* A renamed parent invalidates all its observed cache names,
       * including the weak homebrew correspondence. */
      if(app_data){
       a->stats->cache_like_folders=before_folders;
       a->stats->cache_like_name_matched=before_matched;
       a->stats->cache_like_name_unmatched=before_unmatched;
       a->stats->cache_like_files=before_files;
       a->stats->cache_like_nominal_bytes=before_bytes;
       a->stats->cache_mtime_recent=before_recent;
       a->stats->cache_mtime_unknown=before_unknown;
       a->stats->cache_mtime_no_recent=before_no_recent;
       a->stats->cache_like_partial=1u;
      }
      a->homebrew_name_count=before_names;
      /* The initial directory name no longer designates the scanned
       * inode. Do not publish its stale bytes/files in top-N, JSON or
       * the aggregate, even when the scan was otherwise readable.
       * Keep an explicit partial entry for diagnostic coverage. */
      bytes=0;
      files=0;
      partial=1;
     }
    }
   }
  }
  record_top(a,e->d_name,kind,bytes,files,partial);
  if(a->stats){
   a->stats->entries++;
   if(!strcmp(kind,"homebrew"))a->stats->homebrew++;
   else if(!strcmp(kind,"plugin"))a->stats->plugins++;
   else if(!strcmp(kind,"app-data"))a->stats->app_data++;
   else if(!strcmp(kind,"installed"))a->stats->installed++;
   if(UINT64_MAX-a->stats->nominal_bytes<bytes){
    a->stats->nominal_bytes=UINT64_MAX;a->partial=1;
   }else a->stats->nominal_bytes+=bytes;
  }
  if(a->items++)add(&a->out,",");
  add(&a->out,"{\"path\":");char display[NAME_CAP+2];
  if(snprintf(display,sizeof(display),"/%s",relative_path)<0){a->partial=1;break;}
  quote(&a->out,display);
  add(&a->out,",\"kind\":");quote(&a->out,kind);
  add(&a->out,",\"risk\":\"protected\",\"nominalBytes\":");
  char n[72];snprintf(n,sizeof(n),"%llu",(unsigned long long)bytes);add(&a->out,n);
  snprintf(n,sizeof(n),",\"files\":%zu,\"partial\":%s}",files,partial?"true":"false");add(&a->out,n);
  if(partial)a->partial=1;
  if(a->out.bad||over(a))break;
 }
 if(closedir(d)!=0)a->partial=1;
}
char*cmp5_homebrew_with_stats(const char*root,size_t*length,
 Cmp5HomebrewStats *stats,Cmp5CancelCheck cancel,void *context){
 if(length)*length=0;
 if(!root||!length)return NULL;
 if(stats)memset(stats,0,sizeof(*stats));
 Cmp5HomebrewStats fallback={0};
 Audit*a=(Audit*)calloc(1,sizeof(Audit));
 if(!a)return NULL;
 /* The JSON-only API still needs honest cache-candidate counters. */
 a->stats=stats?stats:&fallback;
 a->cancel=cancel;a->context=context;
 a->cache_wall_now=time(NULL);
 a->out.data=(char*)calloc(1,JSON_CAP);
 if(!a->out.data){free(a);return NULL;}
 uint64_t now=now_ns();
 if(now&&now<=UINT64_MAX-TIME_CAP_NS)a->deadline=now+TIME_CAP_NS;
 add(&a->out,"{\"schemaVersion\":1,\"mode\":\"read-only\",\"scope\":\"protected-homebrew-metadata\",\"items\":[");
 audit_root(a,root,"data/homebrew","homebrew");
 if(!over(a))audit_root(a,root,"data/etaHEN/plugins","plugin");
 if(!over(a))audit_root(a,root,"data","app-data");
 /* Preserve unrelated etaHEN metadata without recounting its plugins. */
 if(!over(a))audit_root(a,root,"data/etaHEN","app-data");
 if(!over(a))audit_root(a,root,"user/app","installed");
 for(int slot=0;slot<8&&!over(a);slot++){
  char path[80];snprintf(path,sizeof(path),"mnt/usb%d/homebrew",slot);audit_root(a,root,path,"homebrew");
  snprintf(path,sizeof(path),"mnt/ext%d/homebrew",slot);audit_root(a,root,path,"homebrew");
  snprintf(path,sizeof(path),"mnt/ext%d/user/app",slot);audit_root(a,root,path,"installed");
 }
 if(over(a))a->partial=1;
 /* No proof of access to either principal root means a partial result,
  * including for an entirely empty/unmounted sandbox. */
 if(a->data_root_state!=1u||a->installed_root_state!=1u)a->partial=1;
 if(stats){
  stats->partial=a->partial?1u:0u;
  stats->data_root_state=a->data_root_state;
  stats->installed_root_state=a->installed_root_state;
 }
 char finish[920];
 const uint32_t caches=a->stats->cache_like_folders;
 const uint32_t cache_files=a->stats->cache_like_files;
 const uint64_t cache_bytes=a->stats->cache_like_nominal_bytes;
 const unsigned cache_partial=a->stats->cache_like_partial;
 const uint32_t matched=a->stats->cache_like_name_matched;
 const uint32_t unmatched=a->stats->cache_like_name_unmatched;
 const int index_partial=a->stats->homebrew_name_index_partial!=0u;
 const uint32_t recent=a->stats->cache_mtime_recent;
 const uint32_t unknown=a->stats->cache_mtime_unknown;
 const uint32_t no_recent=a->stats->cache_mtime_no_recent;
 snprintf(finish,sizeof(finish),
  "],\"visited\":%zu,\"partial\":%s,\"rootCoverage\":{\"appData\":\"%s\",\"installedTitles\":\"%s\"},\"cacheCandidates\":{\"folders\":%u,\"files\":%u,\"nominalBytes\":%llu,\"partial\":%s,\"nameMatched\":%u,\"noNameMatch\":%u,\"nameIndexPartial\":%s,\"recentMtime24h\":%u,\"mtimeUnknown\":%u,\"noRecentMtimeObserved\":%u,\"idleVerified\":false,\"providerVerified\":false,\"deletable\":false},\"limits\":{\"maxEntries\":256,\"maxVisited\":5000,\"maxDepth\":5},\"note\":\"Taille nominale, pas un espace recuperable. Aucun fichier supprimable presume.\"}",
  a->visited,a->partial?"true":"false",
  root_state_name(a->data_root_state),root_state_name(a->installed_root_state),
  caches,cache_files,(unsigned long long)cache_bytes,
  cache_partial?"true":"false",
  matched,unmatched,index_partial?"true":"false",
  recent,unknown,no_recent);
 add(&a->out,finish);
 char*result=a->out.data;
 if(a->out.bad){free(result);result=NULL;}else *length=a->out.len;
 free(a);return result;
}

char*cmp5_homebrew(const char*root,size_t*length){
 return cmp5_homebrew_with_stats(root,length,NULL,NULL,NULL);
}
