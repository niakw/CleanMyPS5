#define _DEFAULT_SOURCE
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
#define DEPTH_CAP 5
#define TIME_CAP_NS 3000000000ULL
typedef struct {char*data;size_t len;int bad;} Writer;
typedef struct {size_t visited,items;int partial;uint64_t deadline;Writer out;
 Cmp5HomebrewStats *stats;Cmp5CancelCheck cancel;void *context;} Audit;
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
static void count_tree(Audit*a,int fd,int depth,uint64_t*size,size_t*files,int*partial){
 if(over(a)){*partial=1;close(fd);return;}
 const int anchored_fd=fd;
 DIR*d=fdopendir(fd);
 if(!d){close(fd);*partial=1;return;}
 struct dirent*e;
 while((e=readdir(d))){
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
  a->visited++;
  if(over(a)){*partial=1;break;}
  struct stat st;
  if(fstatat(anchored_fd,e->d_name,&st,AT_SYMLINK_NOFOLLOW)){*partial=1;continue;}
  if(S_ISLNK(st.st_mode))continue;
  int excluded=e->d_name[0]=='.';
  const char *const *names=S_ISDIR(st.st_mode)?cmp5_private_dirs:cmp5_private_files;
  size_t count=S_ISDIR(st.st_mode)?sizeof(cmp5_private_dirs)/sizeof(*cmp5_private_dirs):sizeof(cmp5_private_files)/sizeof(*cmp5_private_files);
  for(size_t i=0;i<count;i++)if(!strcasecmp(e->d_name,names[i]))excluded=1;
  if(excluded)continue;
  if(S_ISREG(st.st_mode)){
   (*files)++;
   if(st.st_size>=0){
    uint64_t v=(uint64_t)st.st_size;
    if(v>UINT64_MAX-*size){*partial=1;continue;}
    *size+=v;
   }
  }else if(S_ISDIR(st.st_mode)){
   if(depth>=DEPTH_CAP){*partial=1;continue;}
   int child=openat(anchored_fd,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
   if(child<0){*partial=1;continue;}
   count_tree(a,child,depth+1,size,files,partial);
  }
  if(*partial)break;
 }
 closedir(d);
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
static void audit_root(Audit*a,const char*root,const char*relative,const char*kind){
 int fd=open_dir(root,relative);if(fd<0){if(errno!=ENOENT)a->partial=1;return;}
 const int anchored_fd=fd;
 DIR*d=fdopendir(fd);if(!d){close(fd);a->partial=1;return;}
 struct dirent*e;
 while((e=readdir(d))){
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
  a->visited++;
  if(over(a)){a->partial=1;break;}
  struct stat st;
  if(fstatat(dirfd(d),e->d_name,&st,AT_SYMLINK_NOFOLLOW)){a->partial=1;continue;}
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
   int child=openat(dirfd(d),e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
   if(child<0)partial=1;
   else count_tree(a,child,0,&bytes,&files,&partial);
  }
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
 closedir(d);
}
char*cmp5_homebrew_with_stats(const char*root,size_t*length,
 Cmp5HomebrewStats *stats,Cmp5CancelCheck cancel,void *context){
 if(length)*length=0;
 if(!root||!length)return NULL;
 if(stats)memset(stats,0,sizeof(*stats));
 Audit*a=(Audit*)calloc(1,sizeof(Audit));
 if(!a)return NULL;
 a->stats=stats;a->cancel=cancel;a->context=context;
 a->out.data=(char*)calloc(1,JSON_CAP);
 if(!a->out.data){free(a);return NULL;}
 uint64_t now=now_ns();
 if(now&&now<=UINT64_MAX-TIME_CAP_NS)a->deadline=now+TIME_CAP_NS;
 add(&a->out,"{\"schemaVersion\":1,\"mode\":\"read-only\",\"scope\":\"protected-homebrew-metadata\",\"items\":[");
 audit_root(a,root,"data/homebrew","homebrew");
 if(!over(a))audit_root(a,root,"data/etaHEN/plugins","plugin");
 if(!over(a))audit_root(a,root,"data","app-data");
 if(!over(a))audit_root(a,root,"user/app","installed");
 for(int slot=0;slot<8&&!over(a);slot++){
  char path[80];snprintf(path,sizeof(path),"mnt/usb%d/homebrew",slot);audit_root(a,root,path,"homebrew");
  snprintf(path,sizeof(path),"mnt/ext%d/homebrew",slot);audit_root(a,root,path,"homebrew");
  snprintf(path,sizeof(path),"mnt/ext%d/user/app",slot);audit_root(a,root,path,"installed");
 }
 if(over(a))a->partial=1;
 if(stats)stats->partial=a->partial?1u:0u;
 char finish[350];
 snprintf(finish,sizeof(finish),
  "],\"visited\":%zu,\"partial\":%s,\"limits\":{\"maxEntries\":256,\"maxVisited\":5000,\"maxDepth\":5},\"note\":\"Taille nominale et inventaire partiel. Aucune installation n'est declaree orpheline ou supprimable.\"}",
  a->visited,a->partial?"true":"false");
 add(&a->out,finish);
 char*result=a->out.data;
 if(a->out.bad){free(result);result=NULL;}else *length=a->out.len;
 free(a);return result;
}

char*cmp5_homebrew(const char*root,size_t*length){
 return cmp5_homebrew_with_stats(root,length,NULL,NULL,NULL);
}
