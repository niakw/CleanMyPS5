#define _DEFAULT_SOURCE
#include "video.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#define CMP5_VIDEO_PATH_MAX 2048
#define CMP5_VIDEO_MAX_BYTES (64ULL*1024*1024*1024)
int cmp5_open_video(const char *root,const char *path,int *out_fd,uint64_t *out_bytes,const char **mime){
 if(!root||!path||!out_fd||!out_bytes||!mime)return 404;
 *out_fd=-1;*out_bytes=0;*mime=NULL;
 static const char prefix[]="/user/av_contents/video/";
 size_t len=strlen(path),n=sizeof(prefix)-1;
 if(len> CMP5_VIDEO_PATH_MAX||strncmp(path,prefix,n)||!path[n])return 404;
 // No traversal, noncanonical separators, backslashes, or pseudo-hidden components.
 if(path[len-1]=='/'||path[0]!='/'||len<2)return 404;
 for(const char*p=path;*p;p++)if(*p=='\\'||(p>path&&*p=='/'&&p[-1]=='/'))return 404;
 const char *ext=strrchr(path,'.');
 if(!ext)return 404;
 int mp4=!strcasecmp(ext,".mp4"),webm=!strcasecmp(ext,".webm");
 if(!mp4&&!webm)return 404;
 char copy[CMP5_VIDEO_PATH_MAX+1];
 memcpy(copy,path+1,len); // includes trailing NUL
 int dir=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(dir<0)return 404;
 char *save=NULL,*part=strtok_r(copy,"/",&save);
 int status=404;
 while(part){
  if(part[0]=='.'||strchr(part,'\\'))goto done;
  char *next=strtok_r(NULL,"/",&save);
  if(next){
   int child=openat(dir,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
   if(child<0)goto done;
   close(dir);dir=child;
  }else{
   int file=openat(dir,part,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
   if(file<0)goto done;
   struct stat st;
   if(fstat(file,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<12){close(file);goto done;}
   if((uint64_t)st.st_size>CMP5_VIDEO_MAX_BYTES){close(file);status=413;goto done;}
   unsigned char magic[12];
   ssize_t got=read(file,magic,sizeof(magic));
   const unsigned char webm_magic[4]={0x1A,0x45,0xDF,0xA3};
   int valid=webm?got>=4&&memcmp(magic,webm_magic,4)==0:
      got>=8&&memcmp(magic+4,"ftyp",4)==0;
   if(!valid||lseek(file,0,SEEK_SET)<0){close(file);goto done;}
   *out_fd=file;*out_bytes=(uint64_t)st.st_size;
   *mime=mp4?"video/mp4":"video/webm";status=200;goto done;
  }
  part=next;
 }
done:
 close(dir);return status;
}
