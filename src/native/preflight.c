#define _DEFAULT_SOURCE
#include "preflight.h"
#include "scan_rules.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define PREFLIGHT_JSON_MAX 16384u
#define PREFLIGHT_PATH_MAX 256u

typedef struct {
 const char *status;
 const char *reason;
 int writable_hint;
 int read_only_mount;
} PathStatus;

static PathStatus probe(const char *root, const char *relative){
 PathStatus result={"unknown","open-failed",0,0};
 if(!root||!relative||strlen(relative)>=PREFLIGHT_PATH_MAX)return result;
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0){result.status=errno==EACCES?"denied":"root-unavailable";
  result.reason=errno==EACCES?"permission-denied":"open-failed";return result;}
 char parts[PREFLIGHT_PATH_MAX];memcpy(parts,relative,strlen(relative)+1);
 char *save=NULL,*part=strtok_r(parts,"/",&save);
 while(part){
  struct stat st;
  if(fstatat(fd,part,&st,AT_SYMLINK_NOFOLLOW)!=0){
   const int err=errno;close(fd);
   result.status=err==ENOENT?"absent":err==EACCES?"denied":"unknown";
   result.reason=err==ENOENT?"not-found":err==EACCES?"permission-denied":"open-failed";
   return result;
  }
  if(S_ISLNK(st.st_mode)){close(fd);result.status="symlink";result.reason="symlink-blocked";return result;}
  if(!S_ISDIR(st.st_mode)){close(fd);result.status="not-directory";result.reason="not-a-directory";return result;}
  int child=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  if(child<0){const int err=errno;close(fd);
   result.status=err==ENOENT?"absent":err==EACCES?"denied":"unknown";
   result.reason=err==ENOENT?"not-found":err==EACCES?"permission-denied":"open-failed";
   return result;
  }
  close(fd);fd=child;part=strtok_r(NULL,"/",&save);
 }
 struct statvfs volume;
 result.status="accessible";
 result.reason="directory-opened";
 if(fstatvfs(fd,&volume)==0){
  result.read_only_mount=(volume.f_flag & ST_RDONLY)!=0;
  if(result.read_only_mount)result.reason="read-only-mount";
 }
 // A non-destructive indication, not authorization to delete. No files are created.
 result.writable_hint=!result.read_only_mount&&faccessat(fd,".",W_OK,0)==0;
 close(fd);return result;
}

char *cmp5_preflight(const char *root,size_t *out_len){
 if(out_len)*out_len=0;
 if(!out_len||!root)return NULL;
 static const char*paths[]={
  "user/av_contents", "data/etaHEN", "data/ps5_autoloader", "data/pldmgr",
  "mnt/usb0/ps5_autoloader", "mnt/usb1/ps5_autoloader", "mnt/usb2/ps5_autoloader",
  "mnt/usb3/ps5_autoloader", "mnt/usb4/ps5_autoloader", "mnt/usb5/ps5_autoloader",
  "mnt/usb6/ps5_autoloader", "mnt/usb7/ps5_autoloader"
 };
 char*out=(char*)calloc(1,PREFLIGHT_JSON_MAX);
 if(!out)return NULL;
 size_t length=0, accessible=0, denied=0, absent=0, other=0, writable=0, readonly=0;
 int n=snprintf(out,PREFLIGHT_JSON_MAX,
  "{\"schemaVersion\":1,\"mode\":\"read-only\",\"scope\":\"known-paths-only\","
  "\"probeMethod\":\"anchored-directory-and-mount-probe\",\"entries\":[");
 if(n<0||(size_t)n>=PREFLIGHT_JSON_MAX){free(out);return NULL;}length=(size_t)n;
 size_t known=sizeof(paths)/sizeof(paths[0]), rules=sizeof(cmp5_roots)/sizeof(*cmp5_roots);
 for(size_t i=0;i<known+rules;i++){
  const char *path=i<known?paths[i]:cmp5_roots[i-known].path;
  PathStatus result=probe(root,path);
  if(!strcmp(result.status,"accessible"))accessible++;
  else if(!strcmp(result.status,"denied"))denied++;
  else if(!strcmp(result.status,"absent"))absent++;
  else other++;
  writable+=(size_t)result.writable_hint;
  readonly+=(size_t)result.read_only_mount;
  n=snprintf(out+length,PREFLIGHT_JSON_MAX-length,
   "%s{\"path\":\"/%s\",\"status\":\"%s\",\"reason\":\"%s\","
   "\"writableHint\":%s,\"readOnlyMount\":%s}",
   i?",":"",path,result.status,result.reason,
   result.writable_hint?"true":"false",result.read_only_mount?"true":"false");
  if(n<0||(size_t)n>=PREFLIGHT_JSON_MAX-length){free(out);return NULL;}
  length+=(size_t)n;
 }
 n=snprintf(out+length,PREFLIGHT_JSON_MAX-length,
  "],\"summary\":{\"accessible\":%zu,\"denied\":%zu,\"absent\":%zu,\"other\":%zu,"
  "\"writableHints\":%zu,\"readOnlyMounts\":%zu},"
  "\"limitations\":\"Directory probing is read-only. A writable hint is not a cleanup privilege or safety validation.\"}",
  accessible,denied,absent,other,writable,readonly);
 if(n<0||(size_t)n>=PREFLIGHT_JSON_MAX-length){free(out);return NULL;}
 length+=(size_t)n;*out_len=length;return out;
}
