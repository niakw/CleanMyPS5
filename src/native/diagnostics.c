#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
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
/* Compare both an open descriptor and its directory entry, including
 * subsecond timestamps. A successful read must still refer to the
 * CURRENT file under the selected autoload name. */
static int same_diagnostic_file(const struct stat*a,const struct stat*b){
#if defined(__APPLE__)
 const int nanos=a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec&&
   a->st_ctimespec.tv_nsec==b->st_ctimespec.tv_nsec;
#else
 const int nanos=a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
   a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
#endif
 return nanos&&S_ISREG(a->st_mode)&&S_ISREG(b->st_mode)&&
   a->st_nlink==1&&b->st_nlink==1&&
   a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
   a->st_size==b->st_size&&a->st_mtime==b->st_mtime&&
   a->st_ctime==b->st_ctime&&a->st_mode==b->st_mode;
}
static int safe_read(const char*root,const char*rel,char*buf,size_t cap,
 Cmp5CancelCheck cancel,void*context){
 if(cancel&&cancel(context))return -2;
 /* Bounded, read-only traversal. Hold the final PARENT directory open
  * until after the last byte was read, then verify the selected filename
  * still references the same inode. A stale descriptor is not sufficient.
  * Return 0 missing, 1 valid, 2 oversized, -1 unsafe or changed. */
 if(!root||!rel||!buf||cap<2)return -1;
 size_t n=strlen(rel);
 if(n==0||n>=PATH_CAP||rel[0]=='/'||rel[n-1]=='/')return -1;
 for(size_t i=0;i<n;i++)
  if(rel[i]=='\\'||(i&&rel[i]=='/'&&rel[i-1]=='/'))return -1;
 char parts[PATH_CAP];memcpy(parts,rel,n+1);
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return -1;
 char*save=NULL;
 char*part=strtok_r(parts,"/",&save);
 if(!part){close(fd);return -1;}
 int parent=-1;
 const char*leaf=NULL;
 for(;;){
  if(part[0]=='.'){close(fd);return -1;}
  char*next=strtok_r(NULL,"/",&save);
  int child=openat(fd,part,O_RDONLY|O_NOFOLLOW|
    (next?O_DIRECTORY:O_NONBLOCK));
  const int error=errno;
  if(child<0){close(fd);return error==ENOENT?0:-1;}
  if(!next){parent=fd;leaf=part;fd=child;break;}
  close(fd);fd=child;part=next;
 }
 int result=-1;
 struct stat before,after,named;
 if(fstat(fd,&before)!=0||!S_ISREG(before.st_mode)||
    before.st_nlink!=1||before.st_size<0)goto done;
 if((uint64_t)before.st_size>cap-1){result=2;goto done;}
 size_t total=0;
 while(total<cap-1){
  if(cancel&&cancel(context)){result=-2;goto done;}
  ssize_t got=read(fd,buf+total,cap-1-total);
  if(got<0){if(errno==EINTR)continue;goto done;}
  if(got==0)break;
  total+=(size_t)got;
 }
 if(cancel&&cancel(context)){result=-2;goto done;}
 if(total==cap-1){
  char extra;ssize_t got;
  do{got=read(fd,&extra,1);}while(got<0&&errno==EINTR);
  if(got>0){result=2;goto done;}
  if(got<0)goto done;
 }
 /* Configurations must be plain text. Embedded NUL makes all tokens
  * after it invisible to the parser and can hide a live autoload entry. */
 if(memchr(buf,0,total)!=NULL)goto done;
 if((!cancel||!cancel(context))&&fstat(fd,&after)==0&&
    fstatat(parent,leaf,&named,AT_SYMLINK_NOFOLLOW)==0&&
    same_diagnostic_file(&before,&after)&&
    same_diagnostic_file(&before,&named)&&
    (uint64_t)after.st_size==total){
   buf[total]=0;result=1;
 }
done:
 close(fd);close(parent);
 return result;
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
 if(!out->first)put(out,",");
 out->first=0;
 put(out,"{\"path\":\"/");
 put(out,path);
 put(out,"\",\"kind\":\"");
 put(out,kind);
 put(out,"\",\"status\":\"");
 put(out,status);
 put(out,"\"}");
}
static void count_status(Cmp5AutoloadStats*s,int code,const char*status){
 if(code==0){s->missing++;return;}
 s->present++;
 if(code<0)s->unreadable++;
 else if(code==2)s->oversized++;
 else if(strcmp(status,"etaHEN-reference")==0)s->etahen_references++;
 else if(strcmp(status,"other-payload-reference")==0)s->other_references++;
 else if(strcmp(status,"enabled")==0)s->autostart_enabled++;
 else if(strcmp(status,"disabled")==0)s->autostart_disabled++;
 else if(strcmp(status,"no-reference")==0)s->no_reference++;
 else s->unknown++;
}
static void diagnostic_entry(Out*out,Cmp5AutoloadStats*s,
 const char*path,const char*kind,int code,char*contents,int toolbox){
 const char*status=code==1?analyze(contents,toolbox):
  code==2?"too-large":"unreadable";
 count_status(s,code,status);
 if(code!=0)entry(out,path,kind,status);
}
char *cmp5_diagnostics_with_stats_cancel(const char*root,size_t *length,
 Cmp5AutoloadStats *stats,Cmp5CancelCheck cancel,void*context){
 if(length)*length=0;
 if(stats)memset(stats,0,sizeof(*stats));
 if(!root||!length||!stats||(cancel&&cancel(context)))return NULL;
 Cmp5AutoloadStats counts={0};
 counts.checked=11;
 Out out={0};out.buf=(char*)calloc(1,JSON_CAP);if(!out.buf)return NULL;
 out.first=1;put(&out,"{\"schemaVersion\":1,\"source\":\"known local autoload paths, read-only\",\"checked\":11,\"entries\":[");
 const char *internal[]={"data/ps5_autoloader/autoload.txt","data/pldmgr/autoload.txt"};
 const char *toolbox="data/etaHEN/config.ini";
 char *contents=(char*)malloc(READ_CAP+1);if(!contents){free(out.buf);return NULL;}
 for(size_t i=0;i<2;i++){
  if(cancel&&cancel(context))goto aborted;
  int code=safe_read(root,internal[i],contents,READ_CAP+1,cancel,context);
  if(code==-2)goto aborted;
  diagnostic_entry(&out,&counts,internal[i],"autoload",code,contents,0);
 }
 for(int i=0;i<8;i++){
  if(cancel&&cancel(context))goto aborted;
  char path[80];snprintf(path,sizeof(path),"mnt/usb%d/ps5_autoloader/autoload.txt",i);
  int code=safe_read(root,path,contents,READ_CAP+1,cancel,context);
  if(code==-2)goto aborted;
  diagnostic_entry(&out,&counts,path,"autoload",code,contents,0);
 }
 if(cancel&&cancel(context))goto aborted;
 {
  int code=safe_read(root,toolbox,contents,READ_CAP+1,cancel,context);
  if(code==-2)goto aborted;
  diagnostic_entry(&out,&counts,toolbox,"toolbox",code,contents,1);
 }
 if(cancel&&cancel(context))goto aborted;
 free(contents);
 put(&out,"],\"limitations\":\"Autoload par application et autres loaders non couverts. Presence != execution actuelle.\"}");
 if(out.error){free(out.buf);return NULL;}
 *length=out.size;
 *stats=counts;
 return out.buf;
aborted:
 free(contents);
 free(out.buf);
 return NULL;
}
char *cmp5_diagnostics_with_stats(const char*root,size_t *length,
 Cmp5AutoloadStats *stats){
 return cmp5_diagnostics_with_stats_cancel(root,length,stats,NULL,NULL);
}
char *cmp5_diagnostics(const char*root,size_t *length){
 Cmp5AutoloadStats ignored={0};
 return cmp5_diagnostics_with_stats(root,length,&ignored);
}
