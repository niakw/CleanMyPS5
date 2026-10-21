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
 int mount_read_only;
} Probe;

static Probe failed_probe(int error,int root){
 if(error==EACCES||error==EPERM)
  return (Probe){"denied","permission-denied",0,0};
 if(error==ENOENT)return (Probe){"absent","not-found",0,0};
 if(error==ELOOP)return (Probe){"symlink","symlink-blocked",0,0};
 if(error==ENOTDIR)return (Probe){"not-directory","not-a-directory",0,0};
 return (Probe){root?"root-unavailable":"unknown","open-failed",0,0};
}

static Probe dir_probe(const char*root,const char*relative){
 if(!root||!relative||strlen(relative)>=PREFLIGHT_PATH_MAX)
  return (Probe){"unknown","invalid-path",0,0};
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return failed_probe(errno,1);
 char parts[PREFLIGHT_PATH_MAX];
 memcpy(parts,relative,strlen(relative)+1);
 char *save=NULL,*part=strtok_r(parts,"/",&save);
 while(part){
  struct stat st;
  if(fstatat(fd,part,&st,AT_SYMLINK_NOFOLLOW)!=0){
   int error=errno;close(fd);return failed_probe(error,0);
  }
  if(S_ISLNK(st.st_mode)){close(fd);return failed_probe(ELOOP,0);}
  if(!S_ISDIR(st.st_mode)){close(fd);return failed_probe(ENOTDIR,0);}
  int child=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  if(child<0){int error=errno;close(fd);return failed_probe(error,0);}
  close(fd);fd=child;
  part=strtok_r(NULL,"/",&save);
 }
 struct statvfs fs;
 int readonly=0;
#ifdef ST_RDONLY
 if(fstatvfs(fd,&fs)==0 && (fs.f_flag & ST_RDONLY))readonly=1;
#endif
 // This is a *hint*, not a promise: per-operation sandbox policies may
 // still reject filesystem writes, even when faccessat reports W_OK.
 int writable=!readonly && faccessat(fd,".",W_OK,0)==0;
 close(fd);
 return (Probe){"accessible",readonly?"read-only-mount":"directory-opened",writable,readonly};
}

char *cmp5_preflight(const char*root,size_t*out_len){
 if(out_len)*out_len=0;
 if(!root||!out_len)return NULL;
 static const char*paths[]={
  "user/av_contents",
  "data/etaHEN",
  "data/ps5_autoloader",
  "data/pldmgr",
  "mnt/usb0/ps5_autoloader",
  "mnt/usb1/ps5_autoloader",
  "mnt/usb2/ps5_autoloader",
  "mnt/usb3/ps5_autoloader",
  "mnt/usb4/ps5_autoloader",
  "mnt/usb5/ps5_autoloader",
  "mnt/usb6/ps5_autoloader",
  "mnt/usb7/ps5_autoloader"
 };
 char *out=(char*)calloc(1,PREFLIGHT_JSON_MAX);
 if(!out)return NULL;
 size_t length=0;
 int n=snprintf(out,PREFLIGHT_JSON_MAX,
  "{\"schemaVersion\":1,\"mode\":\"read-only\",\"scope\":\"known-paths-only\","
  "\"probeMethod\":\"anchored-directory-and-mount-probe\",\"entries\":[");
 if(n<0||(size_t)n>=PREFLIGHT_JSON_MAX){free(out);return NULL;}
 length=(size_t)n;
 size_t known=sizeof(paths)/sizeof(paths[0]);
 size_t rules=sizeof(cmp5_roots)/sizeof(*cmp5_roots);
 unsigned denied=0,accessible=0,absent=0,readonly=0,writable=0;
 for(size_t i=0;i<known+rules;i++){
  const char *path=i<known?paths[i]:cmp5_roots[i-known].path;
  Probe p=dir_probe(root,path);
  if(!strcmp(p.status,"denied"))denied++;
  if(!strcmp(p.status,"accessible"))accessible++;
  if(!strcmp(p.status,"absent"))absent++;
  if(p.mount_read_only)readonly++;
  if(p.writable_hint)writable++;
  n=snprintf(out+length,PREFLIGHT_JSON_MAX-length,
   "%s{\"path\":\"/%s\",\"status\":\"%s\",\"reason\":\"%s\","
   "\"writableHint\":%s,\"readOnlyMount\":%s}",
   i?",":"",path,p.status,p.reason,
   p.writable_hint?"true":"false",p.mount_read_only?"true":"false");
  if(n<0||(size_t)n>=PREFLIGHT_JSON_MAX-length){free(out);return NULL;}
  length+=(size_t)n;
 }
 n=snprintf(out+length,PREFLIGHT_JSON_MAX-length,
  "],\"summary\":{\"accessible\":%u,\"denied\":%u,\"absent\":%u,"
  "\"readOnlyMounts\":%u,\"writePermissionHints\":%u},"
  "\"limitations\":\"Ouverture des repertoires, droits apparents et drapeaux de montage uniquement. "
  "Ne donne jamais de privileges. Les politiques du sandbox peuvent refuser chaque operation. "
  "Une ecriture possible ne signifie pas qu'un fichier est supprimable sans danger.\"}",
  accessible,denied,absent,readonly,writable);
 if(n<0||(size_t)n>=PREFLIGHT_JSON_MAX-length){free(out);return NULL;}
 length+=(size_t)n;*out_len=length;return out;
}
