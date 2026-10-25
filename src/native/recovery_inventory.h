/* Read-only discovery of interrupted AMPR outputs. Never authorizes resume.
 * The name+checkpoint are observations, not proof of original consent.
 * No Sony files or user saves are opened or modified. */
#ifndef CMP5_RECOVERY_INVENTORY_H
#define CMP5_RECOVERY_INVENTORY_H
#include "compression_allowed_paths.h"
#include <dirent.h>
#include <stdio.h>
#include <stdint.h>

#define CMP5_RECOVERY_MAX 8u
#define CMP5_RECOVERY_DIRECTORY_LIMIT 256u
typedef enum { CMP5_RECOVERY_PACK=1,CMP5_RECOVERY_COPY=2,
               CMP5_RECOVERY_VERIFY=3 } Cmp5RecoveryStage;
typedef struct {
 char title_id[10];
 char destination[48];
 Cmp5RecoveryStage stage;
} Cmp5RecoveryCandidate;
typedef struct {
 Cmp5RecoveryCandidate entries[CMP5_RECOVERY_MAX];
 unsigned count,hidden,folders_checked,unavailable;
 int partial;
} Cmp5RecoveryInventory;

static inline bool cmp5_recovery_title(const char*name){
 if(!name||strlen(name)!=9u)return false;
 for(unsigned i=0;i<9u;i++){
  const char c=name[i];
  if(i<4u?(c<'A'||c>'Z'):(c<'0'||c>'9'))return false;
 }
 return true;
}
static inline int cmp5_recovery_parent_fd(const char*root,const char*dest){
 if(!root||!dest||!cmp5_game_destination_allowed(dest))return -1;
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return -1;
 const char*start=dest+1;
 while(*start){
  const char*slash=strchr(start,'/');
  const size_t len=slash?(size_t)(slash-start):strlen(start);
  char piece[65];
  if(!len||len>=sizeof(piece)){close(fd);return -1;}
  memcpy(piece,start,len);piece[len]=0;
  int next=openat(fd,piece,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  close(fd);
  if(next<0)return -1;
  fd=next;
  if(!slash)break;
  start=slash+1;
 }
 return fd;
}
static inline bool cmp5_recovery_inspect(const char*root,const char*dest,
                                          const char*title,Cmp5RecoveryStage*out){
 if(out)*out=0;
 if(!out||!cmp5_recovery_title(title)||!cmp5_game_destination_allowed(dest))
  return false;
 char state_dir[192];
 const int n=snprintf(state_dir,sizeof(state_dir),"%s/%s/.e2a-state",dest,title);
 if(n<=0||(size_t)n>=sizeof(state_dir))return false;
 int dir=cmp5_game_open_checkpoint_dir(root,state_dir);
 if(dir<0)return false;
 int request=openat(dir,"request.bin",O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 int stage=openat(dir,"stage",O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 struct stat req_first={0},req_after={0},req_named={0};
 struct stat first={0},after={0},named={0},state_first={0},state_after={0};
 bool ok=request>=0&&stage>=0&&
    fstat(dir,&state_first)==0&&S_ISDIR(state_first.st_mode)&&
    fstat(request,&req_first)==0&&S_ISREG(req_first.st_mode)&&
    req_first.st_nlink==1&&req_first.st_size>=512&&req_first.st_size<=4096&&
    fstat(stage,&first)==0&&S_ISREG(first.st_mode)&&first.st_nlink==1&&
    (first.st_size==4||first.st_size==6);
 char value[7]={0};
 if(ok){
  size_t done=0;
  while(done<(size_t)first.st_size){
   ssize_t r=read(stage,value+done,(size_t)first.st_size-done);
   if(r>0)done+=(size_t)r;
   else if(r<0&&errno==EINTR)continue;
   else{ok=false;break;}
  }
  if(ok){
   const char *expected=first.st_size==4?NULL:"verify";
   if(!expected){
    if(!memcmp(value,"pack",4))*out=CMP5_RECOVERY_PACK;
    else if(!memcmp(value,"copy",4))*out=CMP5_RECOVERY_COPY;
    else ok=false;
   }else if(!memcmp(value,expected,6))*out=CMP5_RECOVERY_VERIFY;
   else ok=false;
  }
 }
 if(ok)ok=fstat(stage,&after)==0&&
      fstatat(dir,"stage",&named,AT_SYMLINK_NOFOLLOW)==0&&
      cmp5_game_same_stat(&first,&after)&&cmp5_game_same_stat(&first,&named)&&
      fstat(request,&req_after)==0&&
      fstatat(dir,"request.bin",&req_named,AT_SYMLINK_NOFOLLOW)==0&&
      cmp5_game_same_stat(&req_first,&req_after)&&
      cmp5_game_same_stat(&req_first,&req_named)&&
      fstat(dir,&state_after)==0&&
      cmp5_game_same_stat(&state_first,&state_after);
 if(request>=0)close(request);
 if(stage>=0)close(stage);
 if(ok){
  const int again=cmp5_game_open_checkpoint_dir(root,state_dir);
  struct stat held={0},current={0};
  ok=again>=0&&fstat(dir,&held)==0&&
     fstat(again,&current)==0&&held.st_dev==current.st_dev&&
     held.st_ino==current.st_ino;
  if(again>=0)close(again);
 }
 close(dir);
 if(!ok)*out=0;
 return ok;
}
static inline bool cmp5_scan_recovery_inventory(const char*root,
                                               Cmp5RecoveryInventory*out){
 if(out)memset(out,0,sizeof(*out));
 if(!root||!out)return false;
 for(unsigned storage=0;storage<17u;storage++){
  char dest[48];
  if(storage==0)snprintf(dest,sizeof(dest),"/data/homebrew");
  else snprintf(dest,sizeof(dest),"/mnt/%s%u/homebrew",
                storage&1u?"usb":"ext",(storage-1u)/2u);
  struct stat parent_before={0},parent_after={0};
  if(!cmp5_game_path_stat(root,dest,true,&parent_before)){
   /* A disconnected device is not automatically a corrupt checkpoint. */
   int fd=cmp5_recovery_parent_fd(root,dest);
   if(fd>=0)close(fd);
   else if(errno!=ENOENT)out->unavailable++;
   continue;
  }
  int parent=cmp5_recovery_parent_fd(root,dest);
  if(parent<0){out->unavailable++;out->partial=1;continue;}
  int listing=openat(parent,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  DIR*dir=listing>=0?fdopendir(listing):NULL;
  if(!dir){
   if(listing>=0)close(listing);
   close(parent);out->unavailable++;out->partial=1;continue;
  }
  unsigned scanned=0;
  for(;;){
   errno=0;
   struct dirent*entry=readdir(dir);
   if(!entry){
    if(errno!=0)out->partial=1;
    break;
   }
   if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
   if(++scanned>CMP5_RECOVERY_DIRECTORY_LIMIT){
    out->partial=1;break;
   }
   if(!cmp5_recovery_title(entry->d_name))continue;
   Cmp5RecoveryStage stage=0;
   if(!cmp5_recovery_inspect(root,dest,entry->d_name,&stage))continue;
   if(out->count<CMP5_RECOVERY_MAX){
    Cmp5RecoveryCandidate*c=&out->entries[out->count++];
    memcpy(c->title_id,entry->d_name,10u);
    snprintf(c->destination,sizeof(c->destination),"%s",dest);
    c->stage=stage;
   }else out->hidden++;
  }
  if(closedir(dir)!=0)out->partial=1;
  if(fstat(parent,&parent_after)!=0||
     !cmp5_game_same_stat(&parent_before,&parent_after)||
     !cmp5_game_path_still_same(root,dest,true,&parent_before)){
   out->partial=1;out->count=0;out->hidden=0;
   memset(out->entries,0,sizeof(out->entries));
  }
  close(parent);
  out->folders_checked++;
 }
 return true;
}
#endif
