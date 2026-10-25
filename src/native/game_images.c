#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "game_images.h"
#include "game_scan_identity.h"
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
/* Real exFAT homebrew dumps are commonly DIRECTORIES, not files called
 * *.exfat. Detect an eboot + sce_sys combination without reading game data.
 * Do not report a folder's total unless the bounded traversal completed. */
static int game_root(Audit*a,int parent,const char*name){
 int fd=openat(parent,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0){
  /* A directory visible in the parent cannot be silently skipped. */
  a->stats->partial=1;return -1;
 }
 struct stat eboot,meta;
 if(fstatat(fd,"eboot.bin",&eboot,AT_SYMLINK_NOFOLLOW)!=0){
  if(errno!=ENOENT)a->stats->partial=1;
  close(fd);return -1;
 }
 if(!S_ISREG(eboot.st_mode)||eboot.st_nlink!=1||eboot.st_size<=0){
  close(fd);return -1;
 }
 if(fstatat(fd,"sce_sys",&meta,AT_SYMLINK_NOFOLLOW)!=0){
  if(errno!=ENOENT)a->stats->partial=1;
  close(fd);return -1;
 }
 if(!S_ISDIR(meta.st_mode)){close(fd);return -1;}
 return fd;
}
static int total_game_tree(Audit*a,int fd,unsigned depth,uint64_t*out){
 if(depth>6u||stopped(a)){close(fd);return 0;}
 struct stat before,after;
 if(fstat(fd,&before)!=0){close(fd);return 0;}
 /* PS5 payload libc does not export dirfd(). Keep a stable duplicate
  * before fdopendir takes ownership of fd; fstat on this duplicate can
  * revalidate the very same directory after recursive traversal. */
 int validation_fd=dup(fd);
 if(validation_fd<0){close(fd);return 0;}
 DIR*d=fdopendir(fd);
 if(!d){close(validation_fd);close(fd);return 0;}
 unsigned entries=0;
 int good=1;
 for(;;){
  if(stopped(a)){good=0;break;}
  errno=0;
  struct dirent*entry=readdir(d);
  if(!entry){if(errno)good=0;break;}
  if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
  if(++entries>MAX_PER_ROOT){good=0;break;}
  a->stats->entries_seen++;
  struct stat st;
  if(fstatat(fd,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)!=0){
   good=0;break;
  }
  if(S_ISREG(st.st_mode)){
   if(st.st_nlink!=1||st.st_size<0||
      (uint64_t)st.st_size>UINT64_MAX-*out){good=0;break;}
   *out+=(uint64_t)st.st_size;
  }else if(S_ISDIR(st.st_mode)){
   if(depth==6u){good=0;break;}
   int child=openat(fd,entry->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
   if(child<0||!total_game_tree(a,child,depth+1,out)){
    good=0;break;
   }
  }else{good=0;break;} /* unexpected links or device files */
 }
 /* A descendant can be renamed/added while we walk it without changing
  * the root game's timestamp. Check EACH traversed directory, not only
  * the top-level game root. This is still a bounded advisory inventory. */
 if(fstat(validation_fd,&after)!=0||
    !cmp5_same_game_directory(&before,&after))good=0;
 if(close(validation_fd)!=0)good=0;
 if(closedir(d)!=0)good=0;
 return good;
}
static int safe_label(const char*name){
 const size_t n=strlen(name);
 if(n==0||n>=64u||name[0]=='.')return 0;
 for(size_t i=0;i<n;i++){
  const unsigned char c=(unsigned char)name[i];
  if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
       (c>='0'&&c<='9')||c==' '||c=='.'||c=='_'||
       c=='-'||c=='['||c==']'||c=='('||c==')'))return 0;
 }
 return 1;
}
/* Keep the largest fully known games visible when there are more than
 * twelve candidates. Already-compressed PFSC images remain inventoried
 * but do not displace conversion candidates in the short UI list. */
static int image_better(const Cmp5GameImage*a,const Cmp5GameImage*b){
 const int ac=a->format==CMP5_IMAGE_FFPFSC;
 const int bc=b->format==CMP5_IMAGE_FFPFSC;
 if(ac!=bc)return !ac;
 if(a->nominal_bytes!=b->nominal_bytes)
  return a->nominal_bytes>b->nominal_bytes;
 return strcmp(a->path,b->path)<0;
}
static int folder_better(const Cmp5GameFolder*a,const Cmp5GameFolder*b){
 if(a->size_complete!=b->size_complete)return a->size_complete>b->size_complete;
 if(a->nominal_bytes!=b->nominal_bytes)
  return a->nominal_bytes>b->nominal_bytes;
 return strcmp(a->path,b->path)<0;
}
static void remember_image(Cmp5GameImageStats*s,Cmp5GameImage candidate){
 if(s->shown_images<CMP5_IMAGE_LIST_CAP){
  s->image[s->shown_images++]=candidate;return;
 }
 unsigned worst=0;
 for(unsigned i=1;i<s->shown_images;i++)
  if(image_better(&s->image[worst],&s->image[i]))worst=i;
 if(image_better(&candidate,&s->image[worst]))s->image[worst]=candidate;
 s->hidden_images++;s->partial=1;
}
static void remember_folder(Cmp5GameImageStats*s,Cmp5GameFolder candidate){
 if(s->shown_folders<CMP5_GAME_LIST_CAP){
  s->folder[s->shown_folders++]=candidate;return;
 }
 unsigned worst=0;
 for(unsigned i=1;i<s->shown_folders;i++)
  if(folder_better(&s->folder[worst],&s->folder[i]))worst=i;
 if(folder_better(&candidate,&s->folder[worst]))s->folder[worst]=candidate;
 s->hidden_folders++;s->partial=1;
}
static void sort_shortlist(Cmp5GameImageStats*s){
 for(unsigned i=1;i<s->shown_images;i++){
  Cmp5GameImage candidate=s->image[i];
  unsigned j=i;
  while(j>0&&image_better(&candidate,&s->image[j-1])){
   s->image[j]=s->image[j-1];--j;
  }
  s->image[j]=candidate;
 }
 for(unsigned i=1;i<s->shown_folders;i++){
  Cmp5GameFolder candidate=s->folder[i];
  unsigned j=i;
  while(j>0&&folder_better(&candidate,&s->folder[j-1])){
   s->folder[j]=s->folder[j-1];--j;
  }
  s->folder[j]=candidate;
 }
}
static void scan_game_folder(Audit*a,int parent,
                             const char*relative,const char*name){
 const int folder=game_root(a,parent,name);
 if(folder<0)return; /* Not a recognized homebrew game directory. */
 struct stat before,after,named;
 int stable=fstat(folder,&before)==0;
 uint64_t bytes=0;
 const int complete=stable&&total_game_tree(a,dup(folder),0,&bytes);
 if(stable)stable=fstat(folder,&after)==0&&
  fstatat(parent,name,&named,AT_SYMLINK_NOFOLLOW)==0&&
  S_ISDIR(named.st_mode)&&
  cmp5_same_game_directory(&before,&after)&&
  cmp5_same_game_directory(&before,&named);
 close(folder);
 if(!stable){a->stats->partial=1;return;}
 a->stats->folder_games++;
 /* This is an advisory UI shortlist. Restrict names and length; a
  * conversion must perform a NEW independent preflight before writing. */
 if(safe_label(name)){
  Cmp5GameFolder candidate={0};
  int n=snprintf(candidate.path,sizeof(candidate.path),"/%s/%s",relative,name);
  if(n>0&&(size_t)n<sizeof(candidate.path)){
   memcpy(candidate.label,name,strlen(name)+1u);
   candidate.nominal_bytes=complete?bytes:0;
   candidate.size_complete=complete?1u:0u;
   remember_folder(a->stats,candidate);
  }else{
   a->stats->hidden_folders++;a->stats->partial=1;
  }
 }else{
  a->stats->hidden_folders++;a->stats->partial=1;
 }
 if(!complete){
  a->stats->folder_partials++;
  a->stats->partial=1;
  return;
 }
 if(bytes>UINT64_MAX-a->stats->folder_nominal_bytes){
  a->stats->partial=1;a->stats->folder_partials++;return;
 }
 a->stats->folder_nominal_bytes+=bytes;
 if(bytes>=LARGE_IMAGE){
  if(bytes>UINT64_MAX-a->stats->large_folder_nominal_bytes){
   a->stats->partial=1;a->stats->folder_partials++;return;
  }
  a->stats->large_folders++;
  a->stats->large_folder_nominal_bytes+=bytes;
 }
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
  if(kind<0){
   /* One level of recognized dump folders only. No blind SSD recursion. */
   struct stat candidate;
   if(fstatat(fd,e->d_name,&candidate,AT_SYMLINK_NOFOLLOW)!=0){
    /* An unrecognized suffix may hide a game directory. */
    a->stats->partial=1;continue;
   }
   if(S_ISDIR(candidate.st_mode))
    scan_game_folder(a,fd,relative,e->d_name);
   continue;
  }
  struct stat before,after;
  if(fstatat(fd,e->d_name,&before,AT_SYMLINK_NOFOLLOW)!=0){
   a->stats->partial=1;continue;
  }
  if(S_ISDIR(before.st_mode)){
   /* A recognized suffix is not proof of a disk-image FILE. Extracted
    * exFAT dumps can also be directories named *.exfat; check the
    * same eboot.bin + sce_sys signature as ordinary homebrew folders.
    * Never count a directory as an image or hand it to an image reader. */
   scan_game_folder(a,fd,relative,e->d_name);
   continue;
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
  /* A limited, read-only shortlist of actual image pathnames. The scan
   * never assumes the suffix proves that ShadowMountPlus can mount it. */
  if(safe_label(e->d_name)){
   Cmp5GameImage candidate={0};
   int n=snprintf(candidate.path,sizeof(candidate.path),
                  "/%s/%s",relative,e->d_name);
   if(n>0&&(size_t)n<sizeof(candidate.path)){
    memcpy(candidate.label,e->d_name,strlen(e->d_name)+1u);
    candidate.nominal_bytes=bytes;
    candidate.format=(uint32_t)kind;
    remember_image(a->stats,candidate);
   }else{
    a->stats->hidden_images++;a->stats->partial=1;
   }
  }else{
   a->stats->hidden_images++;a->stats->partial=1;
  }
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
  /* Homebrew games are also commonly stored under etaHEN/games on
   * removable exFAT devices. Enumerate these explicit known roots. */
  (void)snprintf(path,sizeof(path),"mnt/usb%u/etaHEN/games",i);
  scan_one(&a,anchor,path);
  (void)snprintf(path,sizeof(path),"mnt/ext%u/etaHEN/games",i);
  scan_one(&a,anchor,path);
  /* ShadowMount and external tools can also discover top-level images. */
  (void)snprintf(path,sizeof(path),"mnt/usb%u",i);
  scan_one(&a,anchor,path);
  (void)snprintf(path,sizeof(path),"mnt/ext%u",i);
  scan_one(&a,anchor,path);
 }
 close(anchor);
 if(cancel&&cancel(context)){memset(result,0,sizeof(*result));return 0;}
 sort_shortlist(result);
 if(result->scanned_roots==0)result->partial=1;
 return 1; /* A bounded survey, never an exhaustive disk inventory. */
}
