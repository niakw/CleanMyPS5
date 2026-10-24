/* Host-only verified-backup workspace guard.
 * Resolve the pathname, reject protected console roots, then traverse each
 * canonical component with O_NOFOLLOW. Finally check the user-supplied name
 * still refers to the same private directory. Never touches the PS5 Gallery.
 * GPL-3.0-or-later.
 */
#ifndef CMP5_PRIVATE_WORKSPACE_GUARD_H
#define CMP5_PRIVATE_WORKSPACE_GUARD_H
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static inline int cmp5_workspace_prefix(const char*path,const char*root){
 const size_t length=strlen(root);
 return strncmp(path,root,length)==0 &&
   (path[length]=='/'||path[length]=='\0');
}
static inline int cmp5_workspace_name_ok(const char*path){
 if(!path||path[0]!='/'||path[1]=='\0')return 0;
 size_t length=0;
 while(path[length]&&length<PATH_MAX)length++;
 if(length>=PATH_MAX||path[length-1]=='/')return 0;
 for(const char*p=path+1;*p;){
  const char*slash=strchr(p,'/');
  const size_t part=slash?(size_t)(slash-p):strlen(p);
  if(!part||(part==1&&p[0]=='.')||
     (part==2&&p[0]=='.'&&p[1]=='.'))return 0;
  if(!slash)break;
  p=slash+1;
 }
 return 1;
}
static inline int cmp5_workspace_forbidden(const char*canonical){
 const char*protected_roots[]={"/user","/system","/system_ex",
                               "/data/etaHEN","/data/pldmgr"};
 for(size_t i=0;i<sizeof(protected_roots)/sizeof(protected_roots[0]);i++)
  if(cmp5_workspace_prefix(canonical,protected_roots[i]))return 1;
 return 0;
}
static inline int cmp5_open_private_workspace(const char*path){
 if(!cmp5_workspace_name_ok(path))return -1;
 char resolved[PATH_MAX];
 if(!realpath(path,resolved)||!cmp5_workspace_name_ok(resolved)||
    cmp5_workspace_forbidden(resolved))return -1;
 int fd=open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return -1;
 /* Open canonical components from an anchored directory. If a parent
  * changes to a symlink after realpath, O_NOFOLLOW fails closed. */
 char parts[PATH_MAX];
 const size_t n=strlen(resolved);
 memcpy(parts,resolved+1,n);
 char*save=NULL;
 for(char*part=strtok_r(parts,"/",&save);part;
     part=strtok_r(NULL,"/",&save)){
  int child=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  close(fd);
  if(child<0)return -1;
  fd=child;
 }
 struct stat actual,original;
 int alias=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 const int ok=alias>=0&&fstat(fd,&actual)==0&&
   fstat(alias,&original)==0&&
   S_ISDIR(actual.st_mode)&&actual.st_uid==geteuid()&&
   (actual.st_mode&077)==0&&
   actual.st_dev==original.st_dev&&actual.st_ino==original.st_ino;
 if(alias>=0)close(alias);
 if(!ok){close(fd);return -1;}
 return fd;
}
#endif
