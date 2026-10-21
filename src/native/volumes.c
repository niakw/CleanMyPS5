#define _DEFAULT_SOURCE
#include "volumes.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#define PATH_CAP 2048
#define JSON_CAP 4096
static int join(char*dst,size_t cap,const char*base,const char*part){
 size_t n=strlen(base);int r=snprintf(dst,cap,"%s%s%s",base,n&&base[n-1]=='/'?"":"/",part);
 return r>=0&&(size_t)r<cap;
}
static int checked_dir(const char*root,const char*relative,char*dst,size_t size,dev_t*device){
 struct stat st;
 if(lstat(root,&st)!=0||!S_ISDIR(st.st_mode))return 0;
 if(strlen(root)>=size||strlen(relative)>=PATH_CAP)return 0;
 memcpy(dst,root,strlen(root)+1);
 char copy[PATH_CAP];memcpy(copy,relative,strlen(relative)+1);
 char*save=NULL;
 for(char*part=strtok_r(copy,"/",&save);part;part=strtok_r(NULL,"/",&save)){
  char next[PATH_CAP];if(!join(next,sizeof(next),dst,part))return 0;
  if(lstat(next,&st)!=0||!S_ISDIR(st.st_mode))return 0;
  memcpy(dst,next,strlen(next)+1);
 }
 *device=st.st_dev;return 1;
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
 int rc=snprintf(json,JSON_CAP,"{\"schemaVersion\":1,\"scope\":\"mounts-not-total-ssd\",\"volumes\":[");
 if(rc<0||(size_t)rc>=JSON_CAP){free(json);return NULL;}len=(size_t)rc;
 for(int i=0;i<2;i++){
  char path[PATH_CAP];struct statvfs fs;
  uint64_t total=0,freebytes=0,available=0;
  const char*status="unavailable";
  if(checked_dir(root,relatives[i],path,sizeof(path),&device[i])&&statvfs(path,&fs)==0){
   const unsigned long long block=fs.f_frsize?fs.f_frsize:fs.f_bsize;
   if(multiply(fs.f_blocks,block,&total)&&multiply(fs.f_bfree,block,&freebytes)&&
      multiply(fs.f_bavail,block,&available)){
    present[i]=1;status="ok";
   }
  }
  const char*shared=(i==1&&present[0]&&present[1]&&device[0]==device[1])?labels[0]:NULL;
  // All textual values come from compiled-in literals, never external FS names.
  rc=snprintf(json+len,JSON_CAP-len,
   "%s{\"path\":\"%s\",\"status\":\"%s\",\"totalBytes\":%" PRIu64 ","
   "\"freeBytes\":%" PRIu64 ",\"availableBytes\":%" PRIu64 "%s%s%s}",
   i?",":"",labels[i],status,total,freebytes,available,
   shared?",\"sharedWith\":\"":"",shared?shared:"",shared?"\"":"");
  if(rc<0||(size_t)rc>=JSON_CAP-len){free(json);return NULL;}
  len+=(size_t)rc;
 }
 rc=snprintf(json+len,JSON_CAP-len,
  "],\"note\":\"Valeurs statvfs des montages visibles. Ne pas additionner deux volumes sans verifier leur identite. Ce n'est pas la capacite totale PS5.\"}");
 if(rc<0||(size_t)rc>=JSON_CAP-len){free(json);return NULL;}
 len+=(size_t)rc;*bytes=len;return json;
}
