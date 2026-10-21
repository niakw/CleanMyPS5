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
#define JSON_CAP 4096

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
 char *json=(char*)calloc(1,JSON_CAP);if(!json)return NULL;
 const char*relatives[2]={"user/av_contents","data"};
 const char*labels[2]={"/user/av_contents","/data"};
 dev_t device[2]={0,0};int present[2]={0,0};
 size_t len=0;
 int rc=snprintf(json,JSON_CAP,
  "{\"schemaVersion\":1,\"scope\":\"mounts-not-total-ssd\",\"volumes\":[");
 if(rc<0||(size_t)rc>=JSON_CAP){free(json);return NULL;}len=(size_t)rc;
 for(int i=0;i<2;i++){
  uint64_t total=0,freebytes=0,available=0;
  int readonly=0;
  const char*status="unavailable";
  int fd=checked_dir(root,relatives[i],&status);
  if(fd>=0){
   struct statvfs fs;struct stat st;
   if(fstat(fd,&st)==0 && fstatvfs(fd,&fs)==0){
    const unsigned long long block=fs.f_frsize?fs.f_frsize:fs.f_bsize;
    if(multiply(fs.f_blocks,block,&total)&&multiply(fs.f_bfree,block,&freebytes)&&
       multiply(fs.f_bavail,block,&available)){
     device[i]=st.st_dev;present[i]=1;status="ok";
#ifdef ST_RDONLY
     readonly=(fs.f_flag&ST_RDONLY)?1:0;
#endif
    }
   }
   close(fd);
  }
  const char*shared=(i==1&&present[0]&&present[1]&&device[0]==device[1])?labels[0]:NULL;
  rc=snprintf(json+len,JSON_CAP-len,
   "%s{\"path\":\"%s\",\"status\":\"%s\",\"totalBytes\":%" PRIu64 ","
   "\"freeBytes\":%" PRIu64 ",\"availableBytes\":%" PRIu64 ","
   "\"readOnlyMount\":%s%s%s%s}",
   i?",":"",labels[i],status,total,freebytes,available,
   readonly?"true":"false",
   shared?",\"sharedWith\":\"":"",shared?shared:"",shared?"\"":"");
  if(rc<0||(size_t)rc>=JSON_CAP-len){free(json);return NULL;}
  len+=(size_t)rc;
 }
 rc=snprintf(json+len,JSON_CAP-len,
  "],\"note\":\"Mesures effectuees sur des descripteurs de dossiers ancres sans suivre les liens. "
  "Les montages visibles ne representent pas le SSD total. Ces mesures ne donnent aucun droit d'effacement.\"}");
 if(rc<0||(size_t)rc>=JSON_CAP-len){free(json);return NULL;}
 len+=(size_t)rc;*bytes=len;return json;
}
