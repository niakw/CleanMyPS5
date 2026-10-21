#define _DEFAULT_SOURCE
#include "media.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#define CMP5_PREVIEW_MAX (32u*1024u*1024u)
#define CMP5_NAME_MAX 2048
int cmp5_open_preview(const char*root,const char*path,int*out_fd,size_t*out_bytes,const char**mime){
 if(!root||!path||!out_fd||!out_bytes||!mime)return 404;
 *out_fd=-1;*out_bytes=0;*mime=NULL;
 static const char prefix[]="/user/av_contents/photo/NPXS40087/";
 const size_t n=strlen(prefix);
 if(strncmp(path,prefix,n)!=0||!path[n]||strlen(path)>CMP5_NAME_MAX)return 404;
 // A tokenised relative path avoids canonicalisation surprises and literal '..' segments.
 char copy[CMP5_NAME_MAX+1];
 memcpy(copy,path+1,strlen(path));
 const char*ext=strrchr(copy,'.');
 if(!ext)return 404;
 int jpeg=!strcasecmp(ext,".jpg")||!strcasecmp(ext,".jpeg");
 int png=!strcasecmp(ext,".png");
 if(!jpeg&&!png)return 404; // JXR conversion is not supported yet.
 int dir=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(dir<0)return 404;
 char*save=0,*part=strtok_r(copy,"/",&save);
 int result=404;
 // Explicitly forbid empty, traversal, hidden and backslash components.
 // Reject noncanonical slash runs before tokenising.
 for(const char*p=path;*p;p++){
  if(*p=='\\'||(p>path&&*p=='/'&&p[-1]=='/'))goto finish;
 }
 while(part){
  if(strcmp(part,".")==0||strcmp(part,"..")==0||part[0]=='.'||strchr(part,'\\'))goto finish;
  char*next=strtok_r(NULL,"/",&save);
  if(next){
   int sub=openat(dir,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
   if(sub<0)goto finish;
   close(dir);dir=sub;
  }else{
   int file=openat(dir,part,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
   if(file<0)goto finish;
   struct stat st;
   if(fstat(file,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<8){
    close(file);goto finish;
   }
   if((uint64_t)st.st_size>CMP5_PREVIEW_MAX){close(file);result=413;goto finish;}
   unsigned char magic[8];
   ssize_t got=read(file,magic,sizeof(magic));
   static const unsigned char png_magic[8]={137,80,78,71,13,10,26,10};
   if(got!=8||(png?memcmp(magic,png_magic,8)!=0:!(magic[0]==255&&magic[1]==216&&magic[2]==255))){
    close(file);goto finish;
   }
   if(lseek(file,0,SEEK_SET)<0){close(file);goto finish;}
   *out_fd=file;*out_bytes=(size_t)st.st_size;*mime=png?"image/png":"image/jpeg";
   result=200;goto finish;
  }
  part=next;
 }
finish:
 close(dir);return result;
}
