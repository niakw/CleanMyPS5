#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "game_images.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define MAX_VISITED 5000u
#define MAX_PER_ROOT 256u
#define MAX_SEEN 256u
#define DEADLINE_NS 3000000000ULL
#define LARGE_IMAGE (20ULL*1024ULL*1024ULL*1024ULL)
typedef struct {dev_t device;ino_t inode;} Seen;
typedef struct {
 Cmp5GameImageStats *stats;
 Seen seen[MAX_SEEN];unsigned nseen;
 uint64_t deadline;
 Cmp5CancelCheck cancel;void *context;
} Audit;
static uint64_t clock_ns(void){
 struct timespec ts;
 if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0||ts.tv_sec<0)return 0;
 return (uint64_t)ts.tv_sec*1000000000ULL+(uint64_t)ts.tv_nsec;
}
static int stopped(Audit*a){
 if(a->cancel&&a->cancel(a->context)){a->stats->partial=1;return 1;}
 uint64_t n=clock_ns();
 if(a->deadline&&n&&n>=a->deadline){a->stats->partial=1;return 1;}
 if(a->stats->entries_seen>=MAX_VISITED){a->stats->partial=1;return 1;}
 return 0;
}
static int format(const char*name){
 const char*dot=strrchr(name,'.');
 if(!dot||dot==name)return -1;
 if(!strcasecmp(dot,".exfat"))return CMP5_IMAGE_EXFAT;
 if(!strcasecmp(dot,".ffpkg"))return CMP5_IMAGE_FFPKG;
 if(!strcasecmp(dot,".ffpfs"))return CMP5_IMAGE_FFPFS;
 if(!strcasecmp(dot,".ffpfsc"))return CMP5_IMAGE_FFPFSC;
 return -1;
}
static int equal_stat(const struct stat*a,const struct stat*b){
#if defined(__APPLE__)
 int fine=a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec&&
          a->st_ctimespec.tv_nsec==b->st_ctimespec.tv_nsec;
#else
 int fine=a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
          a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
#endif
 return fine&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
   a->st_size==b->st_size&&a->st_mtime==b->st_mtime&&
   a->st_ctime==b->st_ctime&&a->st_mode==b->st_mode&&
   a->st_nlink==b->st_nlink&&S_ISREG(b->st_mode);
}
static int open_child_path(int anchor,const char*relative){
 if(!relative||!relative[0])return dup(anchor);
 char parts[96];size_t n=strlen(relative);
 if(n>=sizeof(parts))return -1;
 memcpy(parts,relative,n+1);
 int fd=dup(anchor);
 if(fd<0)return -1;
 char*save=NULL;
 for(char*part=strtok_r(parts,"/",&save);part;
     part=strtok_r(NULL,"/",&save)){
  if(!strcmp(part,".")||!strcmp(part,"..")||part[0]=='.'){
   close(fd);return -1;
  }
  const int child=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  const int saved_errno=errno;
  close(fd);
  if(child<0){errno=saved_errno;return -1;}
  fd=child;
 }
 return fd;
}
static void scan_one(Audit*a,int parent,const char*relative){
 if(stopped(a))return;
 int fd=open_child_path(parent,relative);
 if(fd<0){
  if(errno==ENOENT)a->stats->missing_roots++;
  else{a->stats->unavailable_roots++;a->stats->partial=1;}
  return;
 }
 a->stats->scanned_roots++;
 DIR*dir=fdopendir(fd);
 if(!dir){close(fd);a->stats->partial=1;return;}
 unsigned visited=0;
 for(;;){
  if(stopped(a))break;
  errno=0;
  struct dirent*e=readdir(dir);
  if(!e){if(errno)a->stats->partial=1;break;}
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
  if(++visited>MAX_PER_ROOT){a->stats->partial=1;break;}
  a->stats->entries_seen++;
  const int kind=format(e->d_name);
  if(kind<0)continue;
  struct stat before,after;
  if(fstatat(fd,e->d_name,&before,AT_SYMLINK_NOFOLLOW)!=0){
   a->stats->partial=1;continue;
  }
  if(!S_ISREG(before.st_mode)||before.st_size<=0){
   a->stats->partial=1;continue;
  }
  if(before.st_nlink!=1){
   a->stats->hardlinks_skipped++;a->stats->partial=1;continue;
  }
  /* A renamed or rewritten entry is not included in any total. Avoid
   * dereferencing symbolic links and exposing untrusted names in results. */
  if(fstatat(fd,e->d_name,&after,AT_SYMLINK_NOFOLLOW)!=0||
     !equal_stat(&before,&after)){
   a->stats->partial=1;continue;
  }
  int duplicate=0;
  for(unsigned i=0;i<a->nseen;i++)
   if(a->seen[i].device==before.st_dev&&
      a->seen[i].inode==before.st_ino)duplicate=1;
  if(duplicate)continue;
  if(a->nseen==MAX_SEEN){a->stats->partial=1;break;}
  a->seen[a->nseen++]=(Seen){before.st_dev,before.st_ino};
  uint64_t bytes=(uint64_t)before.st_size;
  if(bytes>UINT64_MAX-a->stats->nominal_bytes){
   a->stats->partial=1;break;
  }
  a->stats->nominal_bytes+=bytes;
  a->stats->count[kind]++;
  if(bytes>a->stats->largest_bytes)a->stats->largest_bytes=bytes;
  if(kind!=CMP5_IMAGE_FFPFSC&&bytes>=LARGE_IMAGE){
   if(bytes>UINT64_MAX-a->stats->large_nominal_bytes){
    a->stats->partial=1;break;
   }
   a->stats->large_candidates++;
   a->stats->large_nominal_bytes+=bytes;
  }
 }
 if(closedir(dir)!=0)a->stats->partial=1;
}
int cmp5_scan_game_images(const char*root,Cmp5GameImageStats*result,
 Cmp5CancelCheck cancel,void*context){
 if(result)memset(result,0,sizeof(*result));
 if(!root||!result)return 0;
 int anchor=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(anchor<0)return 0;
 Audit a={0};a.stats=result;a.cancel=cancel;a.context=context;
 uint64_t now=clock_ns();
 if(now&&now<UINT64_MAX-DEADLINE_NS)a.deadline=now+DEADLINE_NS;
 scan_one(&a,anchor,"data/homebrew");
 scan_one(&a,anchor,"data/etaHEN/games");
 for(unsigned i=0;i<8&&!stopped(&a);i++){
  char path[48];
  (void)snprintf(path,sizeof(path),"mnt/usb%u/homebrew",i);
  scan_one(&a,anchor,path);
  (void)snprintf(path,sizeof(path),"mnt/ext%u/homebrew",i);
  scan_one(&a,anchor,path);
  /* ShadowMount and external tools can also discover top-level images. */
  (void)snprintf(path,sizeof(path),"mnt/usb%u",i);
  scan_one(&a,anchor,path);
  (void)snprintf(path,sizeof(path),"mnt/ext%u",i);
  scan_one(&a,anchor,path);
 }
 close(anchor);
 if(cancel&&cancel(context)){memset(result,0,sizeof(*result));return 0;}
 if(result->scanned_roots==0)result->partial=1;
 return 1; /* A bounded survey, never an exhaustive disk inventory. */
}
