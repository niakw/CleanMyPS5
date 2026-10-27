#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "capture_companions.h"
#include "media_snapshot.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define MAX_TOTAL (512ULL*1024ULL*1024ULL) /* Host backup contract only. */
static int cancelled(Cmp5CancelCheck check,void*ctx){return check&&check(ctx);}
static int denied_error(int e){return e==EACCES||e==EPERM;}
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
/* A descriptor still describes the original directory after it has been
 * renamed away. Reopen the selected parent's ORIGINAL pathname from a
 * trusted root and require its identity to match before publishing
 * observations. All components use O_NOFOLLOW; no Gallery writes. */
static int gallery_parent_current(const char*root,const char*full,
                                  size_t parent_len,const struct stat*before){
 if(!root||!full||full[0]!='/'||!before||parent_len<=1u||
    parent_len>=sizeof(((Cmp5MediaCandidate*)0)->preview_path)||
    full[parent_len]!='/' )return 0;
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return 0;
 const char*cursor=full+1;
 const char*end=full+parent_len;
 while(cursor<end){
  const char*separator=memchr(cursor,'/',(size_t)(end-cursor));
  const size_t n=separator?(size_t)(separator-cursor):
    (size_t)(end-cursor);
  char component[256];
  if(n==0u||n>=sizeof(component)||
     (n==1u&&cursor[0]=='.')||
     (n==2u&&cursor[0]=='.'&&cursor[1]=='.')){
   close(fd);return 0;
  }
  memcpy(component,cursor,n);component[n]=0;
  int next=openat(fd,component,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  close(fd);
  if(next<0)return 0;
  fd=next;
  cursor=separator?separator+1:end;
 }
 struct stat current={0};
 const int stable=fstat(fd,&current)==0&&S_ISDIR(current.st_mode)&&
   same_file(before,&current);
 close(fd);
 return stable;
}
/* Inventory the KNOWN cross-tree thumbnail directory, not only the
 * expected leaf. An additional same-stem thumbnail, sidecar or hidden
 * transaction is an unsupported dependency, even when .jpg.jpeg exists.
 * This is metadata-only and deliberately does not infer other Sony roots.
 * fdopendir owns an independently opened handle (not dup) so a previous
 * scan's cursor cannot conceal later entries. */
static Cmp5ThumbnailState known_thumbnail_namespace(int parent,
                                                   const char*expected,
                                                   Cmp5CancelCheck cancel,
                                                   void*context){
 static const char suffix[]=".jpg.jpeg";
 const size_t n=strlen(expected),tail=sizeof(suffix)-1u;
 if(n<=tail||strcmp(expected+n-tail,suffix)!=0)
  return CMP5_THUMBNAIL_UNSAFE;
 const size_t stem_len=n-tail;
 int separate=openat(parent,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(separate<0)return errno==EACCES||errno==EPERM?
  CMP5_THUMBNAIL_INACCESSIBLE:CMP5_THUMBNAIL_UNSAFE;
 DIR*scan=fdopendir(separate);
 if(!scan){int err=errno;close(separate);
  return err==EACCES||err==EPERM?
   CMP5_THUMBNAIL_INACCESSIBLE:CMP5_THUMBNAIL_UNSAFE;
 }
 Cmp5ThumbnailState result=CMP5_THUMBNAIL_CANDIDATE_FOUND;
 unsigned visited=0;
 for(;;){
  if(cancelled(cancel,context)){
   result=CMP5_THUMBNAIL_INACCESSIBLE;break;
  }
  errno=0;
  struct dirent*entry=readdir(scan);
  if(!entry){
   if(errno!=0)result=errno==EACCES||errno==EPERM?
    CMP5_THUMBNAIL_INACCESSIBLE:CMP5_THUMBNAIL_UNSAFE;
   break;
  }
  if(++visited>1024u){result=CMP5_THUMBNAIL_UNSAFE;break;}
  const char*name=entry->d_name;
  if(!strcmp(name,".")||!strcmp(name,".."))continue;
  const size_t len=strlen(name);
  const char*maybe=name;
  if(maybe[0]=='.'){++maybe;}
  if(len>=stem_len+1u&&
     strncasecmp(maybe,expected,stem_len)==0&&
     maybe[stem_len]=='.'&&strcmp(name,expected)!=0){
   result=CMP5_THUMBNAIL_UNSAFE;break;
  }
 }
 if(closedir(scan)!=0&&result==CMP5_THUMBNAIL_CANDIDATE_FOUND)
  result=CMP5_THUMBNAIL_UNSAFE;
 return result;
}
/* Read-only cross-directory evidence, never a complete dependency graph.
 * Some gallery implementations reference <stem>.jpg.jpeg thumbnails under
 * /user/av_contents/thumbnails/photo/ with the same relative directory.
 * Other layouts are explicitly UNKNOWN, including index/database links. */
static Cmp5ThumbnailState inspect_known_thumbnail(const char*root,
 const Cmp5MediaCandidate*primary,Cmp5CancelCheck cancel,void*context,
 Cmp5MediaCandidate*thumb){
 if(thumb)memset(thumb,0,sizeof(*thumb));
 static const char photo[]="/user/av_contents/photo/";
 if(!root||!primary||primary->media_type!=1u)return CMP5_THUMBNAIL_NOT_CHECKED;
 const char*path=primary->preview_path;
 /* Advance past the full photo/ prefix. Keeping its last slash would
  * produce a double slash in a candidate backup locator. */
 const size_t plen=strlen(photo);
 const size_t path_len=strnlen(path,sizeof(primary->preview_path));
 if(path_len>=sizeof(primary->preview_path)||path_len<=plen||
    memcmp(path,photo,plen)!=0)return CMP5_THUMBNAIL_UNSAFE;
 const char*slash=strrchr(path,'/');
 const char*dot=strrchr(path,'.');
 if(!slash||!dot||dot<=slash+1||dot+1>=path+path_len)
  return CMP5_THUMBNAIL_UNSAFE;
 /* The prefix has already been snapshot-checked by scan_capture_group. */
 const char*rel=path+plen;
 const size_t stem_len=(size_t)(dot-rel);
 char locator[sizeof(primary->preview_path)];
 const int length=snprintf(locator,sizeof(locator),
    "/user/av_contents/thumbnails/photo/%.*s.jpg.jpeg",(int)stem_len,rel);
 if(length<=0||(size_t)length>=sizeof(locator))
  return CMP5_THUMBNAIL_UNSAFE;
 char candidate[sizeof(primary->preview_path)];
 memcpy(candidate,locator+1,(size_t)length);
 if(cancelled(cancel,context))return CMP5_THUMBNAIL_INACCESSIBLE;
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return CMP5_THUMBNAIL_INACCESSIBLE;
 char*leaf=strrchr(candidate,'/');
 if(!leaf){close(fd);return CMP5_THUMBNAIL_UNSAFE;}
 *leaf++=0;
 char*save=NULL;
 for(char*part=strtok_r(candidate,"/",&save);part;
     part=strtok_r(NULL,"/",&save)){
  if(!*part||part[0]=='.'||cancelled(cancel,context)){
   close(fd);return CMP5_THUMBNAIL_UNSAFE;
  }
  int child=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  if(child<0){
   const int error=errno;
   close(fd);
   return error==ENOENT?CMP5_THUMBNAIL_KNOWN_PATH_ABSENT:
    (error==ELOOP||error==ENOTDIR)?CMP5_THUMBNAIL_UNSAFE:
    CMP5_THUMBNAIL_INACCESSIBLE;
  }
  close(fd);fd=child;
 }
 if(cancelled(cancel,context)){close(fd);return CMP5_THUMBNAIL_INACCESSIBLE;}
 struct stat parent_before={0},parent_after={0},named={0},opened={0};
 /* Snapshot BEFORE enumeration. The directory may change while readdir
  * runs, even when the expected thumbnail remains unchanged. */
 if(fstat(fd,&parent_before)!=0){close(fd);return CMP5_THUMBNAIL_INACCESSIBLE;}
 const Cmp5ThumbnailState siblings=known_thumbnail_namespace(
  fd,leaf,cancel,context);
 if(siblings!=CMP5_THUMBNAIL_CANDIDATE_FOUND){close(fd);return siblings;}
 if(fstat(fd,&parent_after)!=0||
    !same_file(&parent_before,&parent_after)){
  close(fd);return CMP5_THUMBNAIL_UNSAFE;
 }
 if(fstatat(fd,leaf,&named,AT_SYMLINK_NOFOLLOW)!=0){
  const int error=errno;
  struct stat parent_after_absence={0};
  const bool stable_absence=error==ENOENT&&
    fstat(fd,&parent_after_absence)==0&&
    same_file(&parent_before,&parent_after_absence)&&
    gallery_parent_current(root,locator,
                           (size_t)(strrchr(locator,'/')-locator),
                           &parent_before)&&!cancelled(cancel,context);
  close(fd);
  if(stable_absence)return CMP5_THUMBNAIL_KNOWN_PATH_ABSENT;
  return error==EACCES||error==EPERM?
    CMP5_THUMBNAIL_INACCESSIBLE:CMP5_THUMBNAIL_UNSAFE;
 }
 if(!S_ISREG(named.st_mode)||named.st_nlink!=1||named.st_size<=0||
    (uint64_t)named.st_size>64u*1024u*1024u){
  close(fd);return CMP5_THUMBNAIL_UNSAFE;
 }
 int file=openat(fd,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 if(file<0){close(fd);return CMP5_THUMBNAIL_INACCESSIBLE;}
 const int stable=fstat(file,&opened)==0&&same_file(&named,&opened)&&
     opened.st_nlink==1&&fstat(fd,&parent_after)==0&&
     same_file(&parent_before,&parent_after)&&
     gallery_parent_current(root,locator,
                            (size_t)(strrchr(locator,'/')-locator),
                            &parent_before)&&!cancelled(cancel,context);
 if(stable&&thumb){
  thumb->bytes=(uint64_t)opened.st_size;
  thumb->media_type=4u; /* Cross-tree thumbnail, private backup only. */
  memcpy(thumb->preview_path,locator,(size_t)length+1u);
  cmp5_snapshot_capture(thumb,&opened);
 }
 close(file);close(fd);
 return stable?CMP5_THUMBNAIL_CANDIDATE_FOUND:CMP5_THUMBNAIL_UNSAFE;
}
/* Identical planner used by host-only group backup and installed title.
 * No backup/writer implementation is linked with this file. */
static int validate_capture_group_limit(const Cmp5MediaCandidate*,uint32_t,uint64_t);
/* A source path may fit exactly while a same-stem companion with a
 * longer suffix does not. The trailing NUL is part of the capacity budget.
 * Subtract only after bounding the parent, to avoid integer wraparound. */
static int companion_locator_fits(size_t parent_len,size_t filename_len,
                                  size_t capacity){
 return capacity>0u&&parent_len<capacity-1u&&
        filename_len<capacity-parent_len-1u;
}
/* Read-only companion planner. The root and every parent component are opened
 * with O_NOFOLLOW; fstatat does not follow companion symlinks. Any matching
 * suspicious or unreadable file refuses the entire plan (fail closed). */
static int scan_capture_group(const char*root,const Cmp5MediaCandidate*primary,
 Cmp5MediaCandidate out[CMP5_GROUP_MAX],uint32_t*count,
 Cmp5CancelCheck cancel,void*context,uint32_t minimum,uint64_t max_total,
 Cmp5CompanionFailure*failure){
 if(count)*count=0;
 if(failure)*failure=CMP5_COMPANION_FAILURE_UNAVAILABLE;
 if(!root||!primary||!out||!count||
    (primary->media_type!=1&&primary->media_type!=2))return 0;
 if(cancelled(cancel,context)){
  if(failure)*failure=CMP5_COMPANION_FAILURE_CANCELLED;
  return 0;
 }
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
 if(fd<0){
  if(failure&&denied_error(errno))*failure=CMP5_COMPANION_FAILURE_PERMISSION_DENIED;
  return 0;
 }
 char*save=NULL;
 for(char*part=strtok_r(directory,"/",&save);part;part=strtok_r(NULL,"/",&save)){
  if(part[0]=='.'){close(fd);return 0;}
  int child=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  const int error=errno;
  close(fd);
  if(child<0){
   if(failure&&denied_error(error))*failure=CMP5_COMPANION_FAILURE_PERMISSION_DENIED;
   return 0;
  }
  fd=child;
 }
 struct stat selected;
 if(fstatat(fd,slash+1,&selected,AT_SYMLINK_NOFOLLOW)!=0){
  if(failure&&denied_error(errno))*failure=CMP5_COMPANION_FAILURE_PERMISSION_DENIED;
  close(fd);return 0;
 }
 if(!cmp5_snapshot_matches(primary,&selected)){
  if(failure)*failure=CMP5_COMPANION_FAILURE_CHANGED;
  close(fd);return 0;
 }
 const int anchored_fd=fd;
 struct stat parent_before;
 if(fstat(anchored_fd,&parent_before)!=0){close(fd);return 0;}
 DIR*dir=fdopendir(fd);
 if(!dir){
  if(failure&&denied_error(errno))*failure=CMP5_COMPANION_FAILURE_PERMISSION_DENIED;
  close(fd);return 0;
 }
 uint32_t gathered=0,visits=0;uint64_t total=0;
 int invalid=0,found_primary=0;
 struct stat scanned[CMP5_GROUP_MAX];
 struct dirent*entry;
 for(;;){
  errno=0;entry=readdir(dir);
  if(!entry){
   if(errno!=0){
    if(failure&&denied_error(errno))*failure=CMP5_COMPANION_FAILURE_PERMISSION_DENIED;
    invalid=1;
   }
   break;
  }
  if(cancelled(cancel,context)){
   if(failure)*failure=CMP5_COMPANION_FAILURE_CANCELLED;
   invalid=1;break;
  }
  if(++visits>1024u){
   if(failure)*failure=CMP5_COMPANION_FAILURE_DIRECTORY_LIMIT;
   invalid=1;break;
  }
  const char*name=entry->d_name;
  if(name[0]=='.'){
   /* Hidden same-stem remnants can be incomplete thumbnail/metadata
    * transactions. Never silently declare a group complete when one
    * exists; ignore unrelated dotfiles, but reject .REC_001.dat etc. */
   if(strncmp(name+1,stem,stem_length)==0&&
      name[1u+stem_length]=='.'){
    if(failure)*failure=CMP5_COMPANION_FAILURE_AMBIGUOUS;
    invalid=1;break;
   }
   continue;
  }
  if(strncmp(name,stem,stem_length)!=0||name[stem_length]!='.')continue;
  const char*ext=name+stem_length;
  uint32_t type=0;
  if(!strcasecmp(ext,".dat"))type=3;
  else if(primary->media_type==1&&
     (!strcasecmp(ext,".jxr")||!strcasecmp(ext,".jpg")||
      !strcasecmp(ext,".jpeg")||!strcasecmp(ext,".png")))type=1;
  else if(primary->media_type==2&&
     (!strcasecmp(ext,".mp4")||!strcasecmp(ext,".webm")))type=2;
  else {
   /* Other PS5 media-gallery tools report same-stem .meta/.ext files.
    * We do not understand their restore/index semantics; distinguish
    * this known blocker from an arbitrary unknown extension. */
   if(failure)*failure=!strcasecmp(ext,".meta")||
       !strcasecmp(ext,".ext")?
        CMP5_COMPANION_FAILURE_UNSUPPORTED_DEPENDENCY:
        CMP5_COMPANION_FAILURE_AMBIGUOUS;
   invalid=1;break;
  } /* Never claim an unsupported group is complete. */
  struct stat st;
  const int read_stat=fstatat(anchored_fd,name,&st,AT_SYMLINK_NOFOLLOW);
  const int stat_error=errno;
  if(read_stat!=0||!S_ISREG(st.st_mode)||st.st_nlink!=1||
     st.st_size<=0||(type==3&&st.st_size>65536)||
     gathered>=CMP5_GROUP_MAX||
     (uint64_t)st.st_size>max_total-total){
   if(failure)*failure=read_stat!=0&&denied_error(stat_error)?
     CMP5_COMPANION_FAILURE_PERMISSION_DENIED:
     CMP5_COMPANION_FAILURE_AMBIGUOUS;
   invalid=1;break;
  }
  const size_t filename_length=strlen(name);
  /* Need the parent, '/', the full filename AND the trailing NUL.
   * A shorter primary (e.g. .jpg) can coexist with a longer same-stem
   * companion (e.g. .jpeg): the latter must not write its terminator
   * one byte beyond preview_path[256]. parent_len was bounded above. */
  if(!companion_locator_fits(parent_len,filename_length,
                             sizeof(out[0].preview_path))){
   if(failure)*failure=CMP5_COMPANION_FAILURE_AMBIGUOUS;
   invalid=1;break;
  }
  Cmp5MediaCandidate*item=&out[gathered];
  memset(item,0,sizeof(*item));
  memcpy(item->preview_path,path,parent_len+1u);
  memcpy(item->preview_path+parent_len+1u,name,filename_length+1u);
  item->bytes=(uint64_t)st.st_size;
  cmp5_snapshot_capture(item,&st);
  item->media_type=type;
  if(strcmp(name,slash+1)==0){
   /* The primary was validated before enumeration. An in-place rewrite
    * may preserve size, inode and second-resolution mtime while changing
    * content. Compare full nanosecond mtime/ctime before accepting it. */
   if(!same_file(&selected,&st)){
    if(failure)*failure=CMP5_COMPANION_FAILURE_CHANGED;
    invalid=1;break;
   }
   found_primary=1;
  }
  scanned[gathered]=st;
  total+=item->bytes;gathered++;
 }
 /* Detect changes within a single enumeration, not only source/backup time. */
 struct stat parent_after,check;
 if(fstat(anchored_fd,&parent_after)!=0||
    !same_file(&parent_before,&parent_after)){
  if(failure)*failure=CMP5_COMPANION_FAILURE_CHANGED;
  invalid=1;
 }
 for(uint32_t i=0;!invalid&&i<gathered;i++){
  const char*leaf=strrchr(out[i].preview_path,'/');
  const int checked=leaf?
    fstatat(anchored_fd,leaf+1,&check,AT_SYMLINK_NOFOLLOW):-1;
  const int error=errno;
  if(checked!=0||!same_file(&scanned[i],&check)||check.st_nlink!=1){
   if(failure)*failure=checked!=0&&denied_error(error)?
     CMP5_COMPANION_FAILURE_PERMISSION_DENIED:
     CMP5_COMPANION_FAILURE_CHANGED;
   invalid=1;
  }
 }
 if(closedir(dir)!=0){
  if(failure)*failure=CMP5_COMPANION_FAILURE_UNAVAILABLE;
  invalid=1;
 }
 /* A parent renamed after fdopendir can keep returning a consistent
  * snapshot from an orphaned inode. It is not the user's selected album. */
 if(!invalid&&!gallery_parent_current(root,path,parent_len,&parent_before)){
  if(failure)*failure=CMP5_COMPANION_FAILURE_CHANGED;
  invalid=1;
 }
 const int invalid_group=gathered>1u&&
   !validate_capture_group_limit(out,gathered,max_total);
 if(invalid||!found_primary||gathered<minimum||invalid_group){
  if(failure&&*failure==CMP5_COMPANION_FAILURE_UNAVAILABLE){
   if(!invalid&&invalid_group)
    *failure=CMP5_COMPANION_FAILURE_AMBIGUOUS;
   else if(!invalid&&!found_primary)
    *failure=CMP5_COMPANION_FAILURE_CHANGED;
  }
  return 0;
 }
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
 int ok=scan_capture_group(root,primary,out,count,cancel,context,
   2u,MAX_TOTAL,NULL);
 if(ok&&primary&&primary->media_type==1u){
  Cmp5MediaCandidate thumbnail={0};
  const Cmp5ThumbnailState state=inspect_known_thumbnail(
      root,primary,cancel,context,&thumbnail);
  if(state==CMP5_THUMBNAIL_CANDIDATE_FOUND){
   uint64_t total=0;
   for(uint32_t i=0;i<*count;i++)total+=out[i].bytes;
   if(*count>=CMP5_GROUP_MAX||thumbnail.bytes>MAX_TOTAL-total)ok=0;
   else{
    out[(*count)++]=thumbnail;
    ok=validate_capture_group_limit(out,*count,MAX_TOTAL);
   }
  }else if(state!=CMP5_THUMBNAIL_KNOWN_PATH_ABSENT)ok=0;
 }
 /* A private transaction may cover the known thumbnail, but unknown
  * Gallery indexes and other directories still forbid any PS5 deletion. */
 if(!ok&&count)*count=0;
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
 /* Pure metadata: 64-bit addition is checked, and no file body is read.
  * Large video recordings must not inherit the 512-MiB backup limit. */
 Cmp5CompanionFailure failure=CMP5_COMPANION_FAILURE_UNAVAILABLE;
 const int found=scan_capture_group(root,primary,parts,&count,cancel,context,
    1u,UINT64_MAX,&failure);
 Cmp5CompanionReport result={0};
 if(found&&!cancelled(cancel,context)&&count>=1u&&count<=CMP5_GROUP_MAX){
  result.files=count;
  result.state=count==1u?CMP5_COMPANIONS_SINGLE:CMP5_COMPANIONS_MULTIPLE;
  for(uint32_t i=0;i<count;i++){
   if(parts[i].media_type==3u)result.sidecars++;
   else result.media++;
   result.nominal_bytes+=parts[i].bytes; /* scanner enforces no uint64 overflow */
  }
  result.thumbnail=inspect_known_thumbnail(root,primary,cancel,context,NULL);
  if(cancelled(cancel,context)){
   report->failure=CMP5_COMPANION_FAILURE_CANCELLED;
   result.state=CMP5_COMPANIONS_UNVERIFIABLE;
  }else if(result.thumbnail==CMP5_THUMBNAIL_INACCESSIBLE||
           result.thumbnail==CMP5_THUMBNAIL_UNSAFE){
   /* A partial cross-tree inspection cannot publish reassuring counts.
    * Preserve only the no-path failure classification. */
   report->thumbnail=result.thumbnail;
   report->failure=result.thumbnail==CMP5_THUMBNAIL_INACCESSIBLE?
     CMP5_COMPANION_FAILURE_THUMBNAIL_UNAVAILABLE:
     CMP5_COMPANION_FAILURE_THUMBNAIL_UNSAFE;
   result.state=CMP5_COMPANIONS_UNVERIFIABLE;
  }else *report=result;
 }else{
  report->failure=cancelled(cancel,context)?
    CMP5_COMPANION_FAILURE_CANCELLED:failure;
 }
 /* No private paths persist in the returned report or native UI. */
 memset(parts,0,sizeof(parts));
 return result.state!=CMP5_COMPANIONS_UNVERIFIABLE;
}

/* Validate each standalone candidate BEFORE using string searches. A snapshot
 * has a fixed-size char array that may be corrupt and lack its final NUL.
 * strrchr/strcmp on such an array before checking termination can read beyond
 * the object (including through the host-only backup entrypoint). */
static int valid_group_component(const Cmp5MediaCandidate*c,
 const char**extension,size_t*stem_size,const char**relative){
 if(!c||!extension||!stem_size||!relative)return 0;
 const char*path=c->preview_path;
 const size_t length=strnlen(path,sizeof(c->preview_path));
 if(length==0||length>=sizeof(c->preview_path))return 0;
 static const char photo[]="/user/av_contents/photo/";
 static const char video[]="/user/av_contents/video/";
 static const char thumb[]="/user/av_contents/thumbnails/photo/";
 const int is_photo=length>sizeof(photo)-1u&&
   memcmp(path,photo,sizeof(photo)-1u)==0;
 const int is_video=length>sizeof(video)-1u&&
   memcmp(path,video,sizeof(video)-1u)==0;
 const int is_thumb=length>sizeof(thumb)-1u&&
   memcmp(path,thumb,sizeof(thumb)-1u)==0;
 if(!is_photo&&!is_video&&!is_thumb)return 0;
 for(size_t i=0;i<length;i++){
  if(path[i]=='\\'||(path[i]=='/'&&
     (path[i+1]=='/'||path[i+1]=='.')))return 0;
 }
 const char*rel=path+(is_thumb?sizeof(thumb)-1u:
                      is_photo?sizeof(photo)-1u:sizeof(video)-1u);
 const char*slash=strrchr(rel,'/');
 const char*ext=strrchr(rel,'.');
 if(!ext)return 0;
 if(c->media_type==4u&&is_thumb){
  if(length<9u||strcasecmp(path+length-9u,".jpg.jpeg")||
     c->bytes>64u*1024u*1024u)return 0;
  ext=path+length-9u;
 }else if(c->media_type==3u&&(is_photo||is_video)){
  if(strcasecmp(ext,".dat")||c->bytes>65536u)return 0;
 }else if(c->media_type==1u&&is_photo){
  if(strcasecmp(ext,".jxr")&&strcasecmp(ext,".jpg")&&
     strcasecmp(ext,".jpeg")&&strcasecmp(ext,".png"))return 0;
 }else if(c->media_type==2u&&is_video){
  if(strcasecmp(ext,".mp4")&&strcasecmp(ext,".webm"))return 0;
 }else return 0;
 if(ext<=rel||(slash&&ext<=slash+1)||ext-rel>255)return 0;
 const size_t n=(size_t)(ext-(slash?slash+1:rel));
 if(n>96u||n==0u)return 0;
 *relative=rel;*extension=ext;*stem_size=(size_t)(ext-rel);
 return 1;
}
static int validate_capture_group_limit(const Cmp5MediaCandidate *parts,
 uint32_t count,uint64_t max_total){
 if(!parts||count<2u||count>CMP5_GROUP_MAX)return 0;
 const char*first_ext=NULL,*first_rel=NULL;
 size_t stem=0;
 if(!valid_group_component(&parts[0],&first_ext,&stem,&first_rel))return 0;
 /* Equivalent relative names in photo/ and video/ are NOT one album.
  * The external thumbnail belongs to photo only. */
 const char*video_root="/user/av_contents/video/";
 const size_t video_len=strlen(video_root);
 const int first_video=parts[0].media_type==2u||
   (parts[0].media_type==3u&&
    !strncmp(parts[0].preview_path,video_root,video_len));
 uint64_t sum=0;
 unsigned media=0,sidecars=0,thumbnails=0;
 for(unsigned i=0;i<count;i++){
  const Cmp5MediaCandidate*c=&parts[i];
  const char*ext=NULL,*rel=NULL;
  size_t current_stem=0;
  const int member_video=c->media_type==2u||
    (c->media_type==3u&&
     !strncmp(c->preview_path,video_root,video_len));
  if(member_video!=first_video||
     !valid_group_component(c,&ext,&current_stem,&rel)||
     current_stem!=stem||memcmp(first_rel,rel,stem)!=0||
     !c->bytes||c->bytes>max_total-sum)return 0;
  if(c->media_type==3u)sidecars++;
  else if(c->media_type==4u)thumbnails++;
  else media++;
  for(unsigned j=0;j<i;j++){
   const char*prior_ext=NULL,*prior_rel=NULL;
   size_t prior_stem=0;
   if(!valid_group_component(&parts[j],&prior_ext,&prior_stem,&prior_rel)||
      (parts[j].media_type==c->media_type&&strcasecmp(prior_ext,ext)==0)||
      (parts[j].snapshot_inode==c->snapshot_inode&&
       parts[j].snapshot_device==c->snapshot_device))return 0;
  }
  sum+=c->bytes;
 }
 return media>=1u&&sidecars<=1u&&thumbnails<=1u&&sum<=max_total;
}

/* Backup and restore stay capped at 512 MiB in total, independent of the
 * read-only title's ability to describe large captures. */
int cmp5_validate_capture_group(const Cmp5MediaCandidate *parts,uint32_t count){
 return validate_capture_group_limit(parts,count,MAX_TOTAL);
}
