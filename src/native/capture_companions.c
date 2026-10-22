#define _DEFAULT_SOURCE
#include "capture_companions.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define MAX_TOTAL (512ULL*1024ULL*1024ULL)
static int cancelled(Cmp5CancelCheck check,void*ctx){return check&&check(ctx);}
static int same_file(const struct stat*a,const struct stat*b){
#if defined(__APPLE__)
 const int nsec=a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec&&
   a->st_ctimespec.tv_nsec==b->st_ctimespec.tv_nsec;
#else
 const int nsec=a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
   a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
#endif
 return nsec&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
  a->st_size==b->st_size&&a->st_mtime==b->st_mtime&&
  a->st_ctime==b->st_ctime;
}
/* Identical planner used by host-only group backup and installed title.
 * No backup/writer implementation is linked with this file. */
int cmp5_validate_capture_group(const Cmp5MediaCandidate*,uint32_t);
/* Read-only companion planner. The root and every parent component are opened
 * with O_NOFOLLOW; fstatat does not follow companion symlinks. Any matching
 * suspicious or unreadable file refuses the entire plan (fail closed). */
static int scan_capture_group(const char*root,const Cmp5MediaCandidate*primary,
 Cmp5MediaCandidate out[CMP5_GROUP_MAX],uint32_t*count,
 Cmp5CancelCheck cancel,void*context,uint32_t minimum){
 if(count)*count=0;
 if(!root||!primary||!out||!count||
    (primary->media_type!=1&&primary->media_type!=2)||
    cancelled(cancel,context))return 0;
 const char*path=primary->preview_path;
 const char*prefix=primary->media_type==1?
  "/user/av_contents/photo/":"/user/av_contents/video/";
 size_t n=0;
 while(n<sizeof(primary->preview_path)&&path[n])n++;
 if(n==sizeof(primary->preview_path)||n<=strlen(prefix)||
    memcmp(path,prefix,strlen(prefix))!=0)return 0;
 const char*slash=strrchr(path,'/');
 const char*extension=strrchr(path,'.');
 if(!slash||!extension||extension<=slash+1||extension[1]==0)return 0;
 /* A caller-controlled media_type must match the selected file extension.
  * Never infer a valid capture from an incorrectly typed .dat or unknown file. */
 const int valid_primary=primary->media_type==1?
  (!strcasecmp(extension,".jxr")||!strcasecmp(extension,".jpg")||
   !strcasecmp(extension,".jpeg")||!strcasecmp(extension,".png")):
  (!strcasecmp(extension,".mp4")||!strcasecmp(extension,".webm"));
 if(!valid_primary)return 0;
 size_t stem_length=(size_t)(extension-(slash+1));
 if(stem_length>96u)return 0;
 for(size_t i=0;i<n;i++)
  if(path[i]=='\\'||(i&&path[i]=='/'&&path[i-1]=='/'))return 0;
 char stem[100];memcpy(stem,slash+1,stem_length);stem[stem_length]=0;
 /* Keep opened parent stable even when there are renames outside our scan. */
 char directory[sizeof(primary->preview_path)];
 const size_t parent_len=(size_t)(slash-path);
 if(parent_len>=sizeof(directory)-1u)return 0;
 memcpy(directory,path+1,parent_len-1u);directory[parent_len-1u]=0;
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return 0;
 char*save=NULL;
 for(char*part=strtok_r(directory,"/",&save);part;part=strtok_r(NULL,"/",&save)){
  if(part[0]=='.'){close(fd);return 0;}
  int child=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  close(fd);if(child<0)return 0;fd=child;
 }
 struct stat selected;
 if(fstatat(fd,slash+1,&selected,AT_SYMLINK_NOFOLLOW)!=0||
    !S_ISREG(selected.st_mode)||selected.st_nlink!=1||
    selected.st_size<=0||(uint64_t)selected.st_size!=primary->bytes||
    (uint64_t)selected.st_dev!=primary->snapshot_device||
    (uint64_t)selected.st_ino!=primary->snapshot_inode||
    (selected.st_mtime>0?(int64_t)selected.st_mtime:0)!=primary->modified_at){
  close(fd);return 0;
 }
 const int anchored_fd=fd;
 struct stat parent_before;
 if(fstat(anchored_fd,&parent_before)!=0){close(fd);return 0;}
 DIR*dir=fdopendir(fd);
 if(!dir){close(fd);return 0;}
 uint32_t gathered=0,visits=0;uint64_t total=0;
 int invalid=0,found_primary=0;
 struct stat scanned[CMP5_GROUP_MAX];
 struct dirent*entry;
 for(;;){
  errno=0;entry=readdir(dir);
  if(!entry){if(errno!=0)invalid=1;break;}
  if(cancelled(cancel,context)||++visits>1024u){invalid=1;break;}
  const char*name=entry->d_name;
  if(name[0]=='.')continue;
  if(strncmp(name,stem,stem_length)!=0||name[stem_length]!='.')continue;
  const char*ext=name+stem_length;
  uint32_t type=0;
  if(!strcasecmp(ext,".dat"))type=3;
  else if(primary->media_type==1&&
     (!strcasecmp(ext,".jxr")||!strcasecmp(ext,".jpg")||
      !strcasecmp(ext,".jpeg")||!strcasecmp(ext,".png")))type=1;
  else if(primary->media_type==2&&
     (!strcasecmp(ext,".mp4")||!strcasecmp(ext,".webm")))type=2;
  else {invalid=1;break;} /* Unknown matching sidecar: cannot claim complete. */
  struct stat st;
  if(fstatat(anchored_fd,name,&st,AT_SYMLINK_NOFOLLOW)!=0||
     !S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<=0||
     (type==3&&st.st_size>65536)||
     gathered>=CMP5_GROUP_MAX||
     (uint64_t)st.st_size>MAX_TOTAL-total){invalid=1;break;}
  size_t filename_length=strlen(name);
  if(parent_len+filename_length>=sizeof(out[0].preview_path)){invalid=1;break;}
  Cmp5MediaCandidate*item=&out[gathered];
  memset(item,0,sizeof(*item));
  memcpy(item->preview_path,path,parent_len+1u);
  memcpy(item->preview_path+parent_len+1u,name,filename_length+1u);
  item->bytes=(uint64_t)st.st_size;
  item->modified_at=st.st_mtime>0?(int64_t)st.st_mtime:0;
  item->snapshot_device=(uint64_t)st.st_dev;
  item->snapshot_inode=(uint64_t)st.st_ino;
  item->media_type=type;
  if(st.st_dev==selected.st_dev&&st.st_ino==selected.st_ino&&
     strcmp(name,slash+1)==0)found_primary=1;
  scanned[gathered]=st;
  total+=item->bytes;gathered++;
 }
 /* Detect changes within a single enumeration, not only source/backup time. */
 struct stat parent_after,check;
 if(fstat(anchored_fd,&parent_after)!=0||
    !same_file(&parent_before,&parent_after))invalid=1;
 for(uint32_t i=0;!invalid&&i<gathered;i++){
  const char*leaf=strrchr(out[i].preview_path,'/');
  if(!leaf||fstatat(anchored_fd,leaf+1,&check,AT_SYMLINK_NOFOLLOW)!=0||
     !same_file(&scanned[i],&check)||check.st_nlink!=1)invalid=1;
 }
 closedir(dir);
 if(invalid||!found_primary||gathered<minimum||
    (gathered>1u&&!cmp5_validate_capture_group(out,gathered)))return 0;
 /* A stable order is mandatory for group IDs and restore audit. */
 for(unsigned i=1;i<gathered;i++){
  Cmp5MediaCandidate tmp=out[i];unsigned j=i;
  while(j&&strcmp(out[j-1].preview_path,tmp.preview_path)>0){
   out[j]=out[j-1];j--;
  }
  out[j]=tmp;
 }
 *count=gathered;
 return 1;
}

int cmp5_plan_capture_group(const char*root,const Cmp5MediaCandidate*primary,
 Cmp5MediaCandidate out[CMP5_GROUP_MAX],uint32_t*count,
 Cmp5CancelCheck cancel,void*context){
 const int ok=scan_capture_group(root,primary,out,count,cancel,context,2u);
 if(!ok&&out){
  /* Enumeration may have copied private locators into output before a
   * late race, symlink or ambiguous companion invalidated the group. */
  memset(out,0,sizeof(*out)*CMP5_GROUP_MAX);
 }
 return ok;
}

int cmp5_inspect_capture_companions(const char*root,
 const Cmp5MediaCandidate*primary,Cmp5CompanionReport*report,
 Cmp5CancelCheck cancel,void*context){
 if(!report)return 0;
 memset(report,0,sizeof(*report));
 Cmp5MediaCandidate parts[CMP5_GROUP_MAX];
 memset(parts,0,sizeof(parts));
 uint32_t count=0;
 const int found=scan_capture_group(root,primary,parts,&count,cancel,context,1u);
 Cmp5CompanionReport result={0};
 if(found&&!cancelled(cancel,context)&&count>=1u&&count<=CMP5_GROUP_MAX){
  result.files=count;
  result.state=count==1u?CMP5_COMPANIONS_SINGLE:CMP5_COMPANIONS_MULTIPLE;
  for(uint32_t i=0;i<count;i++){
   if(parts[i].media_type==3u)result.sidecars++;
   else result.media++;
   result.nominal_bytes+=parts[i].bytes; /* scanner enforces <=512 MiB */
  }
  *report=result;
 }
 /* No private paths persist in the returned report or native UI. */
 memset(parts,0,sizeof(parts));
 return result.state!=CMP5_COMPANIONS_UNVERIFIABLE;
}

int cmp5_validate_capture_group(const Cmp5MediaCandidate *parts,uint32_t count){
 if(!parts||count<2||count>CMP5_GROUP_MAX)return 0;
 const char*first=parts[0].preview_path;
 const char*dot=strrchr(first,'.');
 const char*slash=strrchr(first,'/');
 if(!dot||!slash||dot<=slash+1)return 0;
 size_t stem=(size_t)(dot-first);
 if(stem>246u)return 0;
 uint64_t sum=0;
 unsigned media=0,sidecars=0;
 for(unsigned i=0;i<count;i++){
  const Cmp5MediaCandidate*c=&parts[i];
  const char*path=c->preview_path;
  size_t n=0;
  while(n<sizeof(c->preview_path)&&path[n])n++;
  const char*ext=strrchr(path,'.');
  if(n==0||n>=sizeof(c->preview_path)||!ext||
     (size_t)(ext-path)!=stem||memcmp(first,path,stem)!=0||
     c->bytes==0||c->bytes>MAX_TOTAL-sum)return 0;
  if(c->media_type!=1&&c->media_type!=2&&c->media_type!=3)return 0;
  if(c->media_type==3){
   if(strcasecmp(ext,".dat")||c->bytes>65536u)return 0;
   sidecars++;
  }else media++;
  for(unsigned j=0;j<i;j++){
   const char*prior_ext=strrchr(parts[j].preview_path,'.');
   if((prior_ext&&strcasecmp(prior_ext,ext)==0)||
      !strcmp(parts[j].preview_path,path)||
      (parts[j].snapshot_inode==c->snapshot_inode&&
       parts[j].snapshot_device==c->snapshot_device))return 0;
  }
  sum+=c->bytes;
 }
 return media>=1&&sidecars<=1&&sum<=MAX_TOTAL;
}
