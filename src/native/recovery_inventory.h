/* Read-only discovery of interrupted AMPR outputs. Never authorizes resume.
 * The name+checkpoint are observations, not proof of original consent.
 * No Sony files or user saves are opened or modified. */
#ifndef CMP5_RECOVERY_INVENTORY_H
#define CMP5_RECOVERY_INVENTORY_H
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdint.h>

#define CMP5_RECOVERY_MAX 8u
#define CMP5_RECOVERY_DIRECTORY_LIMIT 256u
/* Numeric stage type permits the explicit zero = unverified sentinel
 * in both C11 and C++20 consumers of this header. */
typedef unsigned Cmp5RecoveryStage;
enum { CMP5_RECOVERY_PACK=1,CMP5_RECOVERY_COPY=2,
       CMP5_RECOVERY_VERIFY=3 };
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
/* These path checks are a read-only subset of the engine policy.
 * Do not include the engine header here: it also contains output creation. */
static inline bool cmp5_recovery_allowed_base(const char*dest){
 if(!dest)return false;
 if(!strcmp(dest,"/data/homebrew"))return true;
 if(strncmp(dest,"/mnt/",5)!=0)return false;
 const char*p=dest+5;
 if(strncmp(p,"usb",3)&&strncmp(p,"ext",3))return false;
 return p[3]>='0'&&p[3]<='7'&&!strcmp(p+4,"/homebrew");
}
static inline bool cmp5_recovery_same(const struct stat*a,
                                      const struct stat*b){
 if(!a||!b)return false;
#if defined(__APPLE__)
 const bool nano=a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec&&
                 a->st_ctimespec.tv_nsec==b->st_ctimespec.tv_nsec;
#else
 const bool nano=a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
                 a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
#endif
 return nano&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
   a->st_mode==b->st_mode&&a->st_nlink==b->st_nlink&&
   a->st_size==b->st_size&&a->st_mtime==b->st_mtime&&
   a->st_ctime==b->st_ctime;
}
static inline int cmp5_recovery_parent_fd(const char*root,const char*dest){
 if(!root||!dest||!cmp5_recovery_allowed_base(dest))return -1;
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
static inline bool cmp5_recovery_parent_stat(const char*root,
                                             const char*base,struct stat*out){
 if(!out)return false;
 memset(out,0,sizeof(*out));
 int fd=cmp5_recovery_parent_fd(root,base);
 if(fd<0)return false;
 const bool ok=fstat(fd,out)==0&&S_ISDIR(out->st_mode);
 close(fd);
 return ok;
}
static inline bool cmp5_recovery_parent_still_same(const char*root,
                                            const char*base,const struct stat*before){
 struct stat now={0};
 return before&&cmp5_recovery_parent_stat(root,base,&now)&&
        cmp5_recovery_same(before,&now);
}
static inline int cmp5_recovery_open_checkpoint_dir(const char*root,
                                                     const char*state_dir){
 static const char suffix[]="/.e2a-state";
 if(!root||!state_dir)return -1;
 const size_t n=strlen(state_dir),len=sizeof(suffix)-1u;
 if(n<=len||n>=192u||strcmp(state_dir+n-len,suffix))return -1;
 const size_t title_end=n-len;
 if(title_end<10u||state_dir[title_end-10u]!='/')return -1;
 char title[10];
 memcpy(title,state_dir+title_end-9u,9u);title[9]=0;
 if(!cmp5_recovery_title(title))return -1;
 char parent[48];
 const size_t parent_len=title_end-10u;
 if(!parent_len||parent_len>=sizeof(parent))return -1;
 memcpy(parent,state_dir,parent_len);parent[parent_len]=0;
 if(!cmp5_recovery_allowed_base(parent))return -1;
 int fd=cmp5_recovery_parent_fd(root,parent);
 if(fd<0)return -1;
 int title_fd=openat(fd,title,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 close(fd);
 if(title_fd<0)return -1;
 int state_fd=openat(title_fd,".e2a-state",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 struct stat title_a={0},title_b={0},state={0};
 bool ok=state_fd>=0&&fstat(title_fd,&title_a)==0&&
   S_ISDIR(title_a.st_mode)&&fstat(state_fd,&state)==0&&
   S_ISDIR(state.st_mode);
 /* Re-resolve the entire allowlisted path, not only the held inode. */
 int fresh_parent=cmp5_recovery_parent_fd(root,parent);
 int fresh_title=fresh_parent>=0?
   openat(fresh_parent,title,O_RDONLY|O_DIRECTORY|O_NOFOLLOW):-1;
 int fresh_state=fresh_title>=0?
   openat(fresh_title,".e2a-state",O_RDONLY|O_DIRECTORY|O_NOFOLLOW):-1;
 struct stat state_b={0};
 ok=ok&&fresh_state>=0&&fstat(fresh_title,&title_b)==0&&
    cmp5_recovery_same(&title_a,&title_b)&&
    fstat(fresh_state,&state_b)==0&&
    cmp5_recovery_same(&state,&state_b);
 if(fresh_state>=0)close(fresh_state);
 if(fresh_title>=0)close(fresh_title);
 if(fresh_parent>=0)close(fresh_parent);
 close(title_fd);
 if(!ok){if(state_fd>=0)close(state_fd);return -1;}
 return state_fd;
}
static inline bool cmp5_recovery_inspect(const char*root,const char*dest,
                                          const char*title,Cmp5RecoveryStage*out){
 if(out)*out=0;
 if(!out||!cmp5_recovery_title(title)||!cmp5_recovery_allowed_base(dest))
  return false;
 char state_dir[192];
 const int n=snprintf(state_dir,sizeof(state_dir),"%s/%s/.e2a-state",dest,title);
 if(n<=0||(size_t)n>=sizeof(state_dir))return false;
 int dir=cmp5_recovery_open_checkpoint_dir(root,state_dir);
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
      cmp5_recovery_same(&first,&after)&&cmp5_recovery_same(&first,&named)&&
      fstat(request,&req_after)==0&&
      fstatat(dir,"request.bin",&req_named,AT_SYMLINK_NOFOLLOW)==0&&
      cmp5_recovery_same(&req_first,&req_after)&&
      cmp5_recovery_same(&req_first,&req_named)&&
      fstat(dir,&state_after)==0&&
      cmp5_recovery_same(&state_first,&state_after);
 if(request>=0)close(request);
 if(stage>=0)close(stage);
 if(ok){
  const int again=cmp5_recovery_open_checkpoint_dir(root,state_dir);
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
  if(!cmp5_recovery_parent_stat(root,dest,&parent_before)){
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
     !cmp5_recovery_same(&parent_before,&parent_after)||
     !cmp5_recovery_parent_still_same(root,dest,&parent_before)){
   out->partial=1;out->count=0;out->hidden=0;
   memset(out->entries,0,sizeof(out->entries));
  }
  close(parent);
  out->folders_checked++;
 }
 return true;
}
#endif
