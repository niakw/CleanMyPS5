#define _DEFAULT_SOURCE
#include "preflight.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PREFLIGHT_JSON_MAX 4096u
#define PREFLIGHT_PATH_MAX 256u
static const char*dir_status(const char*root,const char*relative){
 if(!root||!relative||strlen(relative)>=PREFLIGHT_PATH_MAX)return "unknown";
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return errno==EACCES?"denied":"root-unavailable";
 char parts[PREFLIGHT_PATH_MAX];memcpy(parts,relative,strlen(relative)+1);
 char*save=NULL,*part=strtok_r(parts,"/",&save);
 while(part){
  struct stat st;
  if(fstatat(fd,part,&st,AT_SYMLINK_NOFOLLOW)!=0){
   const int err=errno;close(fd);
   if(err==ENOENT)return "absent";
   return err==EACCES?"denied":"unknown";
  }
  if(S_ISLNK(st.st_mode)){close(fd);return "symlink";}
  if(!S_ISDIR(st.st_mode)){close(fd);return "not-directory";}
  int child=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  if(child<0){
   const int err=errno;close(fd);
   if(err==ENOENT)return "absent";
   return err==EACCES?"denied":"unknown";
  }
  close(fd);fd=child;
  part=strtok_r(NULL,"/",&save);
 }
 close(fd);return "accessible";
}
char *cmp5_preflight(const char*root,size_t*out_len){
 if(out_len)*out_len=0;
 if(!out_len||!root)return NULL;
 static const char*paths[]={
  "user/av_contents",
  "user/av_contents/photo/NPXS40087",
  "user/av_contents/video/NPXS40087",
  "data",
  "data/etaHEN",
  "data/ps5_autoloader",
  "data/pldmgr",
  "mnt/usb7/ps5_autoloader"
 };
 char*out=(char*)calloc(1,PREFLIGHT_JSON_MAX);
 if(!out)return NULL;
 size_t length=0;
 int n=snprintf(out,PREFLIGHT_JSON_MAX,
  "{\"schemaVersion\":1,\"mode\":\"read-only\",\"scope\":\"known-paths-only\","
  "\"entries\":[");
 if(n<0||(size_t)n>=PREFLIGHT_JSON_MAX){free(out);return NULL;}length=(size_t)n;
 for(size_t i=0;i<sizeof(paths)/sizeof(paths[0]);i++){
  const char*status=dir_status(root,paths[i]);
  n=snprintf(out+length,PREFLIGHT_JSON_MAX-length,
   "%s{\"path\":\"/%s\",\"status\":\"%s\"}",
   i?",":"",paths[i],status);
  if(n<0||(size_t)n>=PREFLIGHT_JSON_MAX-length){free(out);return NULL;}
  length+=(size_t)n;
 }
 n=snprintf(out+length,PREFLIGHT_JSON_MAX-length,
  "],\"limitations\":\"Tests d'ouverture de repertoires uniquement. "
  "Pas de validation du navigateur, des API multimedia ou du firmware.\"}");
 if(n<0||(size_t)n>=PREFLIGHT_JSON_MAX-length){free(out);return NULL;}
 length+=(size_t)n;*out_len=length;return out;
}
