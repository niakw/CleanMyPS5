#define _DEFAULT_SOURCE
#include "diagnostics.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define READ_CAP 65536
#define JSON_CAP 8192
#define PATH_CAP 2048
typedef struct {char *buf;size_t size;int first,error;} Out;
static void put(Out*out,const char *text){
 size_t n=strlen(text);if(out->error||n>JSON_CAP-out->size-1){out->error=1;return;}
 memcpy(out->buf+out->size,text,n+1);out->size+=n;
}
static int contains_ci(const char*text,const char*word){
 size_t n=strlen(word);
 for(const char*p=text;*p;p++){
  size_t k=0;for(;k<n&&p[k];k++)if(tolower((unsigned char)p[k])!=tolower((unsigned char)word[k]))break;
  if(k==n)return 1;
 }
 return 0;
}
static char*trim(char*p){
 while(isspace((unsigned char)*p))p++;
 size_t n=strlen(p);
 while(n&&isspace((unsigned char)p[n-1]))p[--n]=0;
 return p;
}
static int safe_read(const char*root,const char*rel,char*buf,size_t cap){
 /* Open every descendant relative to an already opened directory. A
  * pathname checked with lstat() and reopened later can be replaced with
  * a symlink in between; O_NOFOLLOW at the final leaf is not enough.
  * Return 0 for missing, 1 for valid, 2 for oversized, -1 for unsafe.
  */
 if(!root||!rel||!buf||cap<2)return -1;
 size_t n=strlen(rel);
 if(n==0||n>=PATH_CAP||rel[0]=='/'||rel[n-1]=='/')return -1;
 char parts[PATH_CAP];
 memcpy(parts,rel,n+1);
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return -1;
 char*save=NULL;
 char*part=strtok_r(parts,"/",&save);
 if(!part){close(fd);return -1;}
 for(;;){
  if(part[0]=='.'){close(fd);return -1;}
  char*next=strtok_r(NULL,"/",&save);
  int child=openat(fd,part,O_RDONLY|O_NOFOLLOW|
    (next?O_DIRECTORY:O_NONBLOCK));
  const int error=errno;
  close(fd);
  if(child<0)return error==ENOENT?0:-1;
  fd=child;
  if(!next)break;
  part=next;
 }
 struct stat before,after;
 if(fstat(fd,&before)!=0||!S_ISREG(before.st_mode)||
    before.st_nlink!=1||before.st_size<0){close(fd);return -1;}
 if((uint64_t)before.st_size>cap-1){close(fd);return 2;}
 size_t total=0;
 while(total<cap-1){
  ssize_t got=read(fd,buf+total,cap-1-total);
  if(got<0){if(errno==EINTR)continue;close(fd);return -1;}
  if(got==0)break;
  total+=(size_t)got;
 }
 if(total==cap-1){
  char extra;
  ssize_t got;
  do{got=read(fd,&extra,1);}while(got<0&&errno==EINTR);
  if(got>0){close(fd);return 2;}
  if(got<0){close(fd);return -1;}
 }
 const int stable=fstat(fd,&after)==0&&
   S_ISREG(after.st_mode)&&after.st_nlink==1&&
   before.st_dev==after.st_dev&&before.st_ino==after.st_ino&&
   before.st_size==after.st_size&&
   before.st_mtime==after.st_mtime&&before.st_ctime==after.st_ctime&&
   (uint64_t)after.st_size==total;
 close(fd);
 if(!stable)return -1;
 buf[total]=0;
 return 1;
}
static const char *analyze(char *bytes,int toolbox){
 const char *status=toolbox?"unknown":"no-reference";
 char *save=0;
 for(char*line=strtok_r(bytes,"\r\n",&save);line;line=strtok_r(NULL,"\r\n",&save)){
  char*text=trim(line);
  if(!*text||*text=='#'||*text==';')continue;
  for(char*c=text;*c;c++)if(*c=='#'||*c==';'){*c=0;break;}
  text=trim(text);if(!*text)continue;
  if(toolbox){
   const char*key="toolbox_auto_start";
   const size_t k=strlen(key);
   if(!strncmp(text,key,k)){
    char*value=trim(text+k);
    if(*value!='=')continue;
    value=trim(value+1);
    if(strcmp(value,"1")==0)status="enabled";
    else if(strcmp(value,"0")==0)status="disabled";
   }
  }else if(text[0]!='!'&&text[0]!='@'){
   // Known autoload formats contain one active binary filename per line.
   // Delay directives and comments must not count as payload references.
   if(contains_ci(text,".elf")||contains_ci(text,".bin")){
    if(contains_ci(text,"etahen"))status="etaHEN-reference";
    else if(strcmp(status,"no-reference")==0)status="other-payload-reference";
   }
  }
 }
 return status;
}
static void entry(Out*out,const char*path,const char*kind,const char*status){
 // path/kind/status originate only from fixed literals, never external input.
 if(!out->first)put(out,",");out->first=0;
 put(out,"{\"path\":\"/");put(out,path);put(out,"\",\"kind\":\"");put(out,kind);
 put(out,"\",\"status\":\"");put(out,status);put(out,"\"}");
}
char *cmp5_diagnostics(const char*root,size_t *length){
 if(length)*length=0;if(!root||!length)return NULL;
 Out out={0};out.buf=(char*)calloc(1,JSON_CAP);if(!out.buf)return NULL;
 out.first=1;put(&out,"{\"schemaVersion\":1,\"source\":\"known local autoload paths, read-only\",\"checked\":11,\"entries\":[");
 const char *internal[]={"data/ps5_autoloader/autoload.txt","data/pldmgr/autoload.txt"};
 const char *toolbox="data/etaHEN/config.ini";
 char *contents=(char*)malloc(READ_CAP+1);if(!contents){free(out.buf);return NULL;}
 for(size_t i=0;i<2;i++){
  int code=safe_read(root,internal[i],contents,READ_CAP+1);
  if(code!=0)entry(&out,internal[i],"autoload",
   code==1?analyze(contents,0):code==2?"too-large":"unreadable");
 }
 for(int i=0;i<8;i++){
  char path[80];snprintf(path,sizeof(path),"mnt/usb%d/ps5_autoloader/autoload.txt",i);
  int code=safe_read(root,path,contents,READ_CAP+1);
  if(code!=0)entry(&out,path,"autoload",
    code==1?analyze(contents,0):code==2?"too-large":"unreadable");
 }
 int code=safe_read(root,toolbox,contents,READ_CAP+1);
 if(code!=0)entry(&out,toolbox,"toolbox",
   code==1?analyze(contents,1):code==2?"too-large":"unreadable");
 free(contents);
 put(&out,"],\"limitations\":\"Autoload par application et autres loaders non couverts. Presence != execution actuelle.\"}");
 if(out.error){free(out.buf);return NULL;}
 *length=out.size;return out.buf;
}
