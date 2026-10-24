#define _DEFAULT_SOURCE
#include "duplicates.h"
#include <ctype.h>
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
#define PATH_CAP 2048
#define ITEM_CAP 128
#define VISIT_CAP 8000
#define DEPTH_CAP 10
#define FILE_CAP (32u*1024u*1024u)
#define IO_CAP (128u*1024u*1024u)
#define JSON_CAP (512u*1024u)
#define MAX_MONOTONIC_NS (6000000000ULL)
typedef struct {
 char path[PATH_CAP];
 off_t size;
 dev_t device;
 ino_t inode;
 time_t modified;
 struct stat snapshot; /* Nanosecond timestamps, link count and mode. */
 int paired;
} Item;
/* Whole-second mtime and inode/size alone do not detect in-place rewrites,
 * hardlink changes or replacements during a long byte comparison. */
static int same_snapshot(const struct stat*a,const struct stat*b){
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
   a->st_ctime==b->st_ctime&&a->st_nlink==b->st_nlink&&
   a->st_mode==b->st_mode;
}
typedef struct {Item items[ITEM_CAP];size_t count,visits,hardlinksSkipped,oversizedSkipped;int partial;uint64_t deadline;
 Cmp5CancelCheck should_cancel;void *cancel_context;} Scan;
static uint64_t now_ns(void){
 struct timespec ts;
 if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0||ts.tv_sec<0)return 0;
 return (uint64_t)ts.tv_sec*1000000000ULL+(uint64_t)ts.tv_nsec;
}
static int expired(const Scan*s){
 if(s->should_cancel&&s->should_cancel(s->cancel_context))return 1;
 uint64_t now=now_ns();return s->deadline && now && now>s->deadline;
}
typedef struct {char *data;size_t len;int error;} Json;
static int join(char*out,size_t cap,const char*dir,const char*base){
 size_t n=strlen(dir);int r=snprintf(out,cap,"%s%s%s",dir,n&&dir[n-1]=='/'?"":"/",base);
 return r>=0&&(size_t)r<cap;
}
static int ends(const char*name,const char*ext){
 size_t n=strlen(name),m=strlen(ext);if(n<m)return 0;
 for(size_t i=0;i<m;i++)if(tolower((unsigned char)name[n-m+i])!=(unsigned char)ext[i])return 0;
 return 1;
}
static int is_image(const char *name){
 return ends(name,".png")||ends(name,".jpg")||ends(name,".jpeg")||ends(name,".jxr");
}
// Hold directory FDs throughout traversal; reject symlinks at every component.
static void walk(Scan*s,int fd,const char*dir,int depth){
 /* An inaccessible sibling marks coverage PARTIAL, but must not discard
  * other readable captures. Only cancellation, elapsed budget, visit or
  * candidate limits may halt the bounded directory traversal. */
 if(expired(s)){s->partial=1;close(fd);return;}
 const int anchored_fd=fd; // fdopendir owns fd; dirfd is unavailable in native PS5 libc.
 DIR*d=fdopendir(fd);if(!d){close(fd);s->partial=1;return;}
 struct dirent*entry;
 for(;;){
  /* POSIX uses NULL for both EOF and a read error. Other syscalls in
   * this loop can change errno, so clear it just before every readdir.
   * An I/O error must never yield a "complete" duplicate sample. */
  errno=0;
  entry=readdir(d);
  if(!entry){
   if(errno!=0)s->partial=1;
   break;
  }
  if(expired(s)){s->partial=1;break;}
  if(strcmp(entry->d_name,".")==0||strcmp(entry->d_name,"..")==0)continue;
  if(++s->visits>VISIT_CAP){s->partial=1;break;}
  if((s->visits&63u)==0&&expired(s)){s->partial=1;break;}
  char path[PATH_CAP];
  if(!join(path,sizeof(path),dir,entry->d_name)){s->partial=1;continue;}
  struct stat st;
  if(fstatat(anchored_fd,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)!=0){s->partial=1;continue;}
  if(S_ISLNK(st.st_mode))continue;
  if(S_ISDIR(st.st_mode)){
   if(depth<DEPTH_CAP){
    int child=openat(anchored_fd,entry->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(child<0)s->partial=1;
    else walk(s,child,path,depth+1);
   }else s->partial=1;
  }else if(S_ISREG(st.st_mode)&&is_image(path)&&st.st_size>0&&st.st_size<=(off_t)FILE_CAP){
   /* Hardlinks can exist OUTSIDE the scanned tree. An inode with more
    * than one link is never an independent recoverable copy. Skip every
    * such entry instead of only the second matching path we happened
    * to encounter. This also removes the quadratic inode scan. */
   if(st.st_nlink!=1){s->hardlinksSkipped++;}
   else{
    if(s->count>=ITEM_CAP){s->partial=1;break;}
    Item *i=&s->items[s->count++];
    memcpy(i->path,path,strlen(path)+1);
    i->size=st.st_size;i->device=st.st_dev;i->inode=st.st_ino;
    i->modified=st.st_mtime;i->snapshot=st;
   }
  }else if(S_ISREG(st.st_mode)&&is_image(path)&&
            st.st_size>(off_t)FILE_CAP){
   /* Do not stop useful comparisons, but never report a complete sample
    * if eligible image formats were excluded solely by the size cap. */
   s->oversizedSkipped++;
  }
  /* Recoverable child errors never end the parent's traversal. */
 }
 if(closedir(d)!=0)s->partial=1;
}
// Open only within the capture namespace; verify that the file is the one
// snapshotted during enumeration, not a replacement after the scan.
static int open_snapshot(int root_fd,const Item*i){
 static const char prefix[]="/user/av_contents/photo/";
 const char*path=i->path;
 if(strncmp(path,prefix,sizeof(prefix)-1)!=0||!path[sizeof(prefix)-1]||strlen(path)>=PATH_CAP)return -1;
 for(const char*p=path;*p;p++)if(*p=='\\'||(p>path&&*p=='/'&&p[-1]=='/'))return -1;
 char copy[PATH_CAP];memcpy(copy,path+1,strlen(path));
 // Reuse the already-opened namespace, even if its original pathname moves.
 int fd=dup(root_fd);if(fd<0)return -1;
 char*save=NULL,*part=strtok_r(copy,"/",&save);
 while(part){
  if(part[0]=='.'){close(fd);return -1;}
  char*next=strtok_r(NULL,"/",&save);
  int child=openat(fd,part,next?(O_RDONLY|O_DIRECTORY|O_NOFOLLOW):(O_RDONLY|O_NOFOLLOW|O_NONBLOCK));
  if(child<0){close(fd);return -1;}
  if(!next){
   /* Verify both the opened file and its current directory entry, so a
    * renamed/replaced leaf does not validate an orphaned descriptor. */
   struct stat opened,named;
   const int stable=fstat(child,&opened)==0&&
     fstatat(fd,part,&named,AT_SYMLINK_NOFOLLOW)==0&&
     same_snapshot(&i->snapshot,&opened)&&
     same_snapshot(&i->snapshot,&named);
   close(fd);
   if(!stable){close(child);return -1;}
  }else close(fd);
  fd=child;part=next;
 }
 return fd;
}
static int cmp_items(const void*a,const void*b){
 const Item*x=(const Item*)a,*y=(const Item*)b;
 if(x->size<y->size)return -1;if(x->size>y->size)return 1;
 return strcmp(x->path,y->path);
}
static ssize_t read_full(int fd,unsigned char*buf,size_t count){
 size_t got=0;
 while(got<count){ssize_t n=read(fd,buf+got,count-got);
  if(n<0){if(errno==EINTR)continue;return -1;}
  if(n==0)break;got+=(size_t)n;
 }return (ssize_t)got;
}
// 1 = byte-identical, 0 = different, -1 = inconclusive. Adds actual transferred bytes to budget.
static int compare(int root_fd,const Item*a,const Item*b,size_t *budget,const Scan*s){
 int x=open_snapshot(root_fd,a),y=-1;
 if(x<0)return -1;
 y=open_snapshot(root_fd,b);
 if(y<0){close(x);return -1;}
 struct stat sa,sb;
 if(fstat(x,&sa)||fstat(y,&sb)||
    !same_snapshot(&a->snapshot,&sa)||
    !same_snapshot(&b->snapshot,&sb)){close(x);close(y);return -1;}
 unsigned char p[32768],q[32768];off_t rest=a->size;int result=1;
 while(rest>0){
  size_t n=rest<(off_t)sizeof(p)?(size_t)rest:sizeof(p);
  if(*budget+n*2>IO_CAP||expired(s)){result=-1;break;}
  ssize_t ax=read_full(x,p,n),by=read_full(y,q,n);
  if(ax!=(ssize_t)n||by!=(ssize_t)n){result=-1;break;}
  *budget+=n*2;
  if(memcmp(p,q,n)!=0){result=0;break;}
  rest-=(off_t)n;
 }
 struct stat afterA,afterB;
 if(fstat(x,&afterA)||fstat(y,&afterB)||
    !same_snapshot(&sa,&afterA)||!same_snapshot(&sb,&afterB))
    result=-1;
 if(result==1){
  /* A rename/unlink after opening the two descriptors leaves fstat() and
   * content unchanged. Re-open both names under the same anchored root:
   * never publish a "duplicate" for a pathname that no longer identifies
   * the compared snapshot. This is only needed for an exact match. */
  const int still_a=open_snapshot(root_fd,a);
  const int still_b=open_snapshot(root_fd,b);
  if(still_a<0||still_b<0)result=-1;
  if(still_a>=0)close(still_a);
  if(still_b>=0)close(still_b);
 }
 close(x);close(y);return result;
}
static void add(Json*b,const char*str){
 size_t n=strlen(str);
 if(b->error||n>=JSON_CAP-b->len){b->error=1;return;}
 memcpy(b->data+b->len,str,n+1);b->len+=n;
}
static void quote(Json*b,const char*s){
 add(b,"\"");
 for(const unsigned char*p=(const unsigned char*)s;*p;p++){
  char buf[8];
  if(*p=='"'||*p=='\\'){buf[0]='\\';buf[1]=(char)*p;buf[2]=0;add(b,buf);}
  else if(*p<32){snprintf(buf,sizeof(buf),"\\u%04x",(unsigned)*p);add(b,buf);}
  else{buf[0]=(char)*p;buf[1]=0;add(b,buf);}
 }
 add(b,"\"");
}
char *cmp5_duplicates_with_stats_cancel(const char*root,size_t*length,Cmp5DuplicateStats *stats,Cmp5CancelCheck should_cancel,void *context){
 if(stats)memset(stats,0,sizeof(*stats));
 if(length)*length=0;if(!root||!length)return NULL;
 // Open first, then inspect that descriptor. A pathname pre-check can race
 // with replacement; every subsequent lookup stays anchored to this handle.
 int root_fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(root_fd<0)return NULL;
 struct stat st;
 if(fstat(root_fd,&st)||!S_ISDIR(st.st_mode)){close(root_fd);return NULL;}
 Scan*s=(Scan*)calloc(1,sizeof(Scan));
 if(!s){close(root_fd);return NULL;}
 s->should_cancel=should_cancel;
 s->cancel_context=context;
 uint64_t started=now_ns();
 if(started && started<=UINT64_MAX-MAX_MONOTONIC_NS)s->deadline=started+MAX_MONOTONIC_NS;
 // Open the known parent components relative to anchored directory handles.
 char dir[PATH_CAP]="/",step[PATH_CAP];
 int fd=dup(root_fd),valid=fd>=0;
 const char*parts[]={"user","av_contents","photo"};
 for(size_t k=0;valid&&k<3;k++){
  if(!join(step,sizeof(step),dir,parts[k])){valid=0;break;}
  int child=openat(fd,parts[k],O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  if(child<0){valid=0;break;}
  close(fd);fd=child;memcpy(dir,step,strlen(step)+1);
 }
 if(valid)walk(s,fd,dir,0);
 else{if(fd>=0)close(fd);s->partial=1;}
 qsort(s->items,s->count,sizeof(Item),cmp_items);
 Json out={0};out.data=(char*)calloc(1,JSON_CAP);
 if(!out.data){close(root_fd);free(s);return NULL;}
 add(&out,"{\"schemaVersion\":1,\"mode\":\"read-only\",\"algorithm\":\"byte-compare\",\"groups\":[");
 size_t budget=0;int groups=0;
 for(size_t i=0;i<s->count;i++){
  if(expired(s)){s->partial=1;break;}
  if(s->items[i].paired)continue;
  size_t matches[ITEM_CAP],nm=1;matches[0]=i;
  for(size_t j=i+1;j<s->count&&s->items[j].size==s->items[i].size;j++){
   if(s->items[j].paired)continue;
   if(budget>=IO_CAP||expired(s)){s->partial=1;break;}
   int result=compare(root_fd,&s->items[i],&s->items[j],&budget,s);
   if(result==1){matches[nm++]=j;s->items[j].paired=1;}
   else if(result<0)s->partial=1;
  }
  if(nm>1){
   if(stats){
    stats->exact_groups++;
    stats->matching_files+=(uint32_t)nm;
    const uint64_t bytes=(uint64_t)s->items[i].size*(uint64_t)(nm-1);
    stats->duplicate_nominal_bytes+=bytes; // Not necessarily reclaimable.
   }
   if(groups++)add(&out,",");
   char n[40];snprintf(n,sizeof(n),"%llu",(unsigned long long)s->items[i].size);
   add(&out,"{\"bytesEach\":");add(&out,n);add(&out,",\"paths\":[");
   for(size_t k=0;k<nm;k++){if(k)add(&out,",");quote(&out,s->items[matches[k]].path);}
   add(&out,"]}");
  }
 }
 if(expired(s)||s->oversizedSkipped||s->hardlinksSkipped)s->partial=1;
 char report_stats[512];
 snprintf(report_stats,sizeof(report_stats),"],\"filesConsidered\":%zu,\"visited\":%zu,\"hardlinksSkipped\":%zu,\"oversizedImagesSkipped\":%zu,\"comparedBytes\":%zu,\"partial\":%s,"
  "\"limits\":{\"maxImages\":128,\"maxFileBytes\":33554432,\"maxReadBytes\":134217728,\"softDurationSeconds\":6},"
  "\"warning\":\"Images uniquement. Fichiers multi-liens exclus; doublons hors echantillon possibles.\"}",
  s->count,s->visits,s->hardlinksSkipped,s->oversizedSkipped,budget,s->partial?"true":"false");
 add(&out,report_stats);
 if(stats){
  stats->files_considered=(uint32_t)s->count;
  stats->hardlinks_skipped=(uint32_t)s->hardlinksSkipped;
  stats->oversized_skipped=(uint32_t)s->oversizedSkipped;
  stats->compared_bytes=budget;
  stats->partial=s->partial?1u:0u;
 }
 close(root_fd);free(s);
 if(out.error){free(out.data);return NULL;}
 *length=out.len;return out.data;
}

char *cmp5_duplicates_with_stats(const char*root,size_t*length,Cmp5DuplicateStats *stats){
 return cmp5_duplicates_with_stats_cancel(root,length,stats,NULL,NULL);
}
char *cmp5_duplicates(const char*root,size_t*length){
 return cmp5_duplicates_with_stats_cancel(root,length,NULL,NULL,NULL);
}
