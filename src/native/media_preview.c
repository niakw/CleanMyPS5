#define _DEFAULT_SOURCE
#include "media_preview.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
 size_t n=0;
 while(n<sizeof(candidate->preview_path)&&path[n])n++;
 if(n==0||n>=sizeof(candidate->preview_path)||
    n<=sizeof(prefix)-1||memcmp(path,prefix,sizeof(prefix)-1)!=0)return NULL;
 const char*extension=strrchr(path,'.');
 if(!extension)return NULL;
 const int jxr=strcasecmp(extension,".jxr")==0;
 const int png=strcasecmp(extension,".png")==0;
 const int jpeg=strcasecmp(extension,".jpg")==0||strcasecmp(extension,".jpeg")==0;
 if(!jxr&&!png&&!jpeg)return NULL;
 char copy[PATH_LIMIT];
 memcpy(copy,path+1,n);copy[n-1]=0;
 // Reject noncanonical paths *before* tokenizing; strtok_r collapses //.
 for(size_t i=0;i<n;i++){
  if(path[i]=='\\'||(i&&path[i]=='/'&&path[i-1]=='/'))return NULL;
 }
 if(path[n-1]=='/')return NULL;
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return NULL;
 char*save=NULL;
 int directory_fd=-1;
 for(char*part=strtok_r(copy,"/",&save);part;){
  if(part[0]=='.'||strchr(part,'\\')){close(fd);return NULL;}
  char*next=strtok_r(NULL,"/",&save);
  if(next){
   const int child=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
   close(fd);
   if(child<0)return NULL;
   fd=child;
  }else{
   directory_fd=fd;
   const int selected=openat(directory_fd,part,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
   if(selected<0){close(directory_fd);return NULL;}
   struct stat original;
   if(fstat(selected,&original)!=0||!matches(&original,candidate)){
    close(selected);close(directory_fd);return NULL;
   }
   int read_fd=selected;
   struct stat source=original;
   if(jxr){
    /* An HDR JXR is never decoded as JPEG. It can have an SDR JPG twin
     * alongside it, as in the 13.60 gallery layout. Open the twin through
     * the SAME anchored directory; never follow symlinks or trust filenames. */
    char companion[PATH_LIMIT];
    const size_t filename_length=strlen(part);
    if(filename_length<4u||filename_length>=sizeof(companion)-5u){
     close(selected);close(directory_fd);return NULL;
    }
    memcpy(companion,part,filename_length+1u);
    memcpy(companion+filename_length-4u,".jpg",5u);
    read_fd=openat(directory_fd,companion,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(read_fd<0){close(selected);close(directory_fd);return NULL;}
    if(fstat(read_fd,&source)!=0||!S_ISREG(source.st_mode)||
       source.st_size<8||source.st_size>CMP5_PREVIEW_FILE_CAP){
     close(read_fd);close(selected);close(directory_fd);return NULL;
    }
   }
   close(directory_fd);
   if(source.st_size<8||source.st_size>CMP5_PREVIEW_FILE_CAP){
    if(jxr)close(read_fd);
    close(selected);return NULL;
   }
   const size_t target=(size_t)source.st_size;
   uint8_t*bytes=(uint8_t*)malloc(target);
   if(!bytes){if(jxr)close(read_fd);close(selected);return NULL;}
   size_t obtained=0;
   while(obtained<target){
    const ssize_t count=read(read_fd,bytes+obtained,target-obtained);
    if(count<0&&errno==EINTR)continue;
    if(count<=0){free(bytes);if(jxr)close(read_fd);close(selected);return NULL;}
    obtained+=(size_t)count;
   }
   struct stat current,still_original;
   const int stable=fstat(read_fd,&current)==0&&
     S_ISREG(current.st_mode)&&current.st_dev==source.st_dev&&
     current.st_ino==source.st_ino&&current.st_size==source.st_size&&
     current.st_mtime==source.st_mtime&&
     fstat(selected,&still_original)==0&&matches(&still_original,candidate);
   if(jxr)close(read_fd);
   close(selected);
   if(!stable){free(bytes);return NULL;}
   if(jxr||jpeg){
    if(target<3||bytes[0]!=0xff||bytes[1]!=0xd8||bytes[2]!=0xff){
     free(bytes);return NULL;
    }
   }else{
    static const uint8_t signature[8]={137,80,78,71,13,10,26,10};
    if(memcmp(bytes,signature,sizeof(signature))!=0){free(bytes);return NULL;}
   }
   *length=obtained;
   return bytes;
  }
  part=next;
 }
 close(fd);return NULL;
}
