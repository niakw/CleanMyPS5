#define _DEFAULT_SOURCE
#include "media_preview.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_LIMIT 256
static int matches(const struct stat *st,const Cmp5MediaCandidate *c){
 return S_ISREG(st->st_mode)&&st->st_size>0&&
  (uint64_t)st->st_size==c->bytes &&
  (uint64_t)st->st_dev==c->snapshot_device &&
  (uint64_t)st->st_ino==c->snapshot_inode &&
  (st->st_mtime>0?(int64_t)st->st_mtime:0)==c->modified_at;
}
uint8_t *cmp5_read_photo_snapshot(const char *root,
 const Cmp5MediaCandidate *candidate,size_t *length){
 if(length)*length=0;
 if(!root||!candidate||!length||candidate->media_type!=1u||
    candidate->bytes==0||candidate->bytes>CMP5_PREVIEW_FILE_CAP)return NULL;
 static const char prefix[]="/user/av_contents/photo/";
 const char*path=candidate->preview_path;
 const size_t n=strnlen(path,sizeof(candidate->preview_path));
 if(n==0||n>=sizeof(candidate->preview_path)||
    n<=sizeof(prefix)-1||memcmp(path,prefix,sizeof(prefix)-1)!=0)return NULL;
 const char*extension=strrchr(path,'.');
 if(!extension||
   (strcasecmp(extension,".png")!=0&&strcasecmp(extension,".jpg")!=0&&
    strcasecmp(extension,".jpeg")!=0))return NULL;
 char copy[PATH_LIMIT];
 memcpy(copy,path+1,n);copy[n-1]=0;
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return NULL;
 char*save=NULL;
 for(char*part=strtok_r(copy,"/",&save);part;){
  if(part[0]=='.'||strchr(part,'\\')){close(fd);return NULL;}
  char*next=strtok_r(NULL,"/",&save);
  const int flags=next?(O_RDONLY|O_DIRECTORY|O_NOFOLLOW):
     (O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
  const int child=openat(fd,part,flags);
  close(fd);
  if(child<0)return NULL;
  fd=child;part=next;
 }
 struct stat st;
 if(fstat(fd,&st)!=0||!matches(&st,candidate)){close(fd);return NULL;}
 uint8_t*bytes=(uint8_t*)malloc((size_t)candidate->bytes);
 if(!bytes){close(fd);return NULL;}
 size_t obtained=0;
 while(obtained<(size_t)candidate->bytes){
  const ssize_t count=read(fd,bytes+obtained,(size_t)candidate->bytes-obtained);
  if(count<0&&errno==EINTR)continue;
  if(count<=0){free(bytes);close(fd);return NULL;}
  obtained+=(size_t)count;
 }
 // Revalidate descriptor and metadata; fail closed on a changed capture.
 if(fstat(fd,&st)!=0||!matches(&st,candidate)){
  free(bytes);close(fd);return NULL;
 }
 close(fd);*length=obtained;return bytes;
}
