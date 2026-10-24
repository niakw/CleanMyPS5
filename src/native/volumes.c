#define _DEFAULT_SOURCE
#include "volumes.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define PATH_CAP 2048
#define JSON_CAP 8192

// Never reopen a path after checking it: each directory is anchored by fd.
static int checked_dir(const char*root,const char*relative,const char**status){
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0){*status=errno==EACCES||errno==EPERM?"denied":"unavailable";return -1;}
 char parts[PATH_CAP];size_t len=strlen(relative);
 if(len>=sizeof(parts)){close(fd);*status="unavailable";return -1;}
 memcpy(parts,relative,len+1);
 char *save=NULL,*part=strtok_r(parts,"/",&save);
 while(part){
  struct stat st;
  if(fstatat(fd,part,&st,AT_SYMLINK_NOFOLLOW)!=0){
   int error=errno;close(fd);
   *status=error==EACCES||error==EPERM?"denied":error==ENOENT?"absent":"unavailable";
   return -1;
  }
  if(S_ISLNK(st.st_mode)){close(fd);*status="symlink";return -1;}
  if(!S_ISDIR(st.st_mode)){close(fd);*status="not-directory";return -1;}
  int child=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  if(child<0){
   int error=errno;close(fd);
   *status=error==EACCES||error==EPERM?"denied":error==ENOENT?"absent":"unavailable";
   return -1;
  }
  close(fd);fd=child;part=strtok_r(NULL,"/",&save);
 }
 return fd;
}

static int multiply(unsigned long long a,unsigned long long b,uint64_t*output){
 if(a&&b>UINT64_MAX/a)return 0;
 *output=(uint64_t)a*(uint64_t)b;return 1;
}

char *cmp5_volumes(const char*root,size_t*bytes){
 if(bytes)*bytes=0;if(!root||!bytes)return NULL;
 /* Two internal observables and sixteen removable/extended mount roots.
  * The roots identify VISIBLE mounts, not the complete physical SSD. */
 enum { ROOTS=18 };
 char *json=(char*)calloc(1,JSON_CAP);if(!json)return NULL;
 dev_t device[ROOTS]={0};
 int first_seen[ROOTS];
 for(int i=0;i<ROOTS;i++)first_seen[i]=-1;
 size_t len=0;
 int rc=snprintf(json,JSON_CAP,
  "{\"schemaVersion\":1,\"scope\":\"mounts-not-total-ssd\",\"volumes\":[");
 if(rc<0||(size_t)rc>=JSON_CAP){free(json);return NULL;}
 len=(size_t)rc;
 for(int i=0;i<ROOTS;i++){
  char rel[48],label[48];
  if(i==0){
   snprintf(rel,sizeof(rel),"user/av_contents");
   snprintf(label,sizeof(label),"/user/av_contents");
  }else if(i==1){
   snprintf(rel,sizeof(rel),"data");
   snprintf(label,sizeof(label),"/data");
  }else{
   const int number=i<10?i-2:i-10;
   const char*mount=i<10?"usb":"ext";
   snprintf(rel,sizeof(rel),"mnt/%s%d",mount,number);
   snprintf(label,sizeof(label),"/mnt/%s%d",mount,number);
  }
  uint64_t total=0,freebytes=0,available=0;
  int readonly=0;
  const char*status="unavailable";
  int fd=checked_dir(root,rel,&status);
  int shared_index=-1;
  if(fd>=0){
   struct statvfs fs;struct stat st;
   if(fstat(fd,&st)==0&&fstatvfs(fd,&fs)==0){
    const unsigned long long block=fs.f_frsize?fs.f_frsize:fs.f_bsize;
    if(block&&multiply(fs.f_blocks,block,&total)&&
       multiply(fs.f_bfree,block,&freebytes)&&
       multiply(fs.f_bavail,block,&available)&&
       total>0&&total>=freebytes&&freebytes>=available){
     status="ok";
#ifdef ST_RDONLY
     readonly=(fs.f_flag&ST_RDONLY)?1:0;
#endif
     /* Only the first observed pathname on a device is canonical.
      * Same-device aliases MUST NOT become extra capacity totals. */
     device[i]=st.st_dev;
     for(int previous=0;previous<i;previous++)
      if(first_seen[previous]>=0&&device[previous]==st.st_dev){
       shared_index=first_seen[previous];break;
      }
     first_seen[i]=shared_index>=0?shared_index:i;
    }else status="unavailable";
   }
   close(fd);
  }
  char shared_path[48]={0};
  if(shared_index==0)snprintf(shared_path,sizeof(shared_path),"/user/av_contents");
  else if(shared_index==1)snprintf(shared_path,sizeof(shared_path),"/data");
  else if(shared_index>=2){
   const int index=shared_index<10?shared_index-2:shared_index-10;
   snprintf(shared_path,sizeof(shared_path),"/mnt/%s%d",
     shared_index<10?"usb":"ext",index);
  }
  const char*shared=shared_index>=0?shared_path:NULL;
  rc=snprintf(json+len,JSON_CAP-len,
   "%s{\"path\":\"%s\",\"status\":\"%s\",\"totalBytes\":%" PRIu64 ","
   "\"freeBytes\":%" PRIu64 ",\"availableBytes\":%" PRIu64 ","
   "\"readOnlyMount\":%s%s%s%s}",
   i?",":"",label,status,total,freebytes,available,
   readonly?"true":"false",
   shared?",\"sharedWith\":\"":"",shared?shared:"",shared?"\"":"");
  if(rc<0||(size_t)rc>=JSON_CAP-len){free(json);return NULL;}
  len+=(size_t)rc;
 }
 rc=snprintf(json+len,JSON_CAP-len,
  "],\"note\":\"Mount capacities and available space are observations only. "
  "Aliases sharing the same filesystem are not additional free space. "
  "No value here authorizes deleting data or predicts recoverable bytes.\"}");
 if(rc<0||(size_t)rc>=JSON_CAP-len){free(json);return NULL;}
 len+=(size_t)rc;*bytes=len;return json;
}
