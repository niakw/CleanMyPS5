#define _DEFAULT_SOURCE
#include "video_metadata.h"
#include "media_snapshot.h"
#include "webm_metadata.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CMP5_VIDEO_MAX_FILE (64ULL*1024u*1024u*1024u)
#define CMP5_BOX_BUDGET 384u
#define CMP5_META_READ_BUDGET (32u*1024u)
#define CMP5_DEPTH_LIMIT 5u
#define CMP5_PATH_MAX 256u

static uint32_t be32(const uint8_t*p){
 return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
        ((uint32_t)p[2]<<8)|(uint32_t)p[3];
}
static uint64_t be64(const uint8_t*p){
 return ((uint64_t)be32(p)<<32)|(uint64_t)be32(p+4);
}
static int same_snapshot_stat(const struct stat*a,const struct stat*b){
#if defined(__APPLE__)
 const int nsec=a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec&&
    a->st_ctimespec.tv_nsec==b->st_ctimespec.tv_nsec;
#else
 const int nsec=a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
    a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
#endif
 return nsec&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
    a->st_size==b->st_size&&a->st_mtime==b->st_mtime&&
    a->st_ctime==b->st_ctime&&a->st_nlink==b->st_nlink&&
    a->st_mode==b->st_mode;
}
static int same_file(const struct stat*st,const Cmp5MediaCandidate*c){
 return cmp5_snapshot_matches(c,st);
}
typedef struct {
 int fd;
 Cmp5CancelCheck cancel;
 void*context;
 uint32_t read_budget;
 uint32_t count;
 uint64_t duration_ms;
 uint32_t width,height;
 int found_movie,found_duration,found_video;
} Mp4;
static int read_at(Mp4*p,uint64_t off,uint8_t*dest,size_t count){
 if(count>CMP5_META_READ_BUDGET-p->read_budget||off>(uint64_t)INT64_MAX)return 0;
 if(p->cancel&&p->cancel(p->context))return 0;
 p->read_budget+=(uint32_t)count;
 if(lseek(p->fd,(off_t)off,SEEK_SET)<0)return 0;
 size_t done=0;
 while(done<count){
  if(p->cancel&&p->cancel(p->context))return 0;
  ssize_t n=read(p->fd,dest+done,count-done);
  if(n<0&&errno==EINTR)continue;
  if(n<=0)return 0;
  done+=(size_t)n;
 }
 return 1;
}
static int boxes(Mp4*p,uint64_t start,uint64_t end,unsigned depth){
 if(depth>CMP5_DEPTH_LIMIT||start>end)return 0;
 for(uint64_t pos=start;pos<end;){
  if(++p->count>CMP5_BOX_BUDGET)return 0;
  if(end-pos<8)return 0;
  uint8_t head[16]={0};
  if(!read_at(p,pos,head,8))return 0;
  uint64_t box_size=be32(head),header=8u;
  if(box_size==1){
   if(end-pos<16||!read_at(p,pos+8,head+8,8))return 0;
   box_size=be64(head+8);header=16u;
  }else if(box_size==0)box_size=end-pos;
  if(box_size<header||box_size>end-pos)return 0;
  const uint64_t payload=pos+header,plen=box_size-header;
  const uint8_t*type=head+4;
  if(depth==0 && pos==0 && memcmp(type,"ftyp",4)!=0)return 0;
  if(depth==0 && memcmp(type,"moov",4)==0){
   p->found_movie=1;
   if(!boxes(p,payload,pos+box_size,depth+1))return 0;
  }else if(depth==1 && memcmp(type,"trak",4)==0){
   if(!boxes(p,payload,pos+box_size,depth+1))return 0;
  }else if(depth==1 && memcmp(type,"mvhd",4)==0){
   uint8_t b[32]={0};
   if(plen<20||!read_at(p,payload,b,plen<32?(size_t)plen:32))return 0;
   const int version=b[0];
   uint32_t timescale=0;
   uint64_t duration=0;
   if(version==0){
    timescale=be32(b+12);duration=be32(b+16);
   }else if(version==1){
    if(plen<32)return 0;
    timescale=be32(b+20);duration=be64(b+24);
   }else return 0;
   if(timescale!=0&&duration>0&&duration!=UINT64_MAX&&duration!=UINT32_MAX){
    // Avoid duration * 1000 overflow, plus unreasonable UI values.
    if(duration<UINT64_MAX/1000u){
     p->duration_ms=(duration*1000u)/timescale;
     p->found_duration=p->duration_ms>0;
    }
   }
  }else if(depth==2 && memcmp(type,"tkhd",4)==0){
   if(plen<84)return 0;
   uint8_t head_version;
   if(!read_at(p,payload,&head_version,1))return 0;
   if(head_version==0&&plen<84)return 0;
   if(head_version==1&&plen<96)return 0;
   if(head_version>1)return 0;
   uint8_t dimensions[8];
   if(!read_at(p,payload+plen-8u,dimensions,8))return 0;
   uint32_t w=be32(dimensions)>>16;
   uint32_t h=be32(dimensions+4)>>16;
   if(w>0&&h>0&&w<=16384&&h<=16384){
    if(!p->found_video||(uint64_t)w*h>(uint64_t)p->width*p->height){
     p->width=w;p->height=h;
    }
    p->found_video=1;
   }
  }
  pos+=box_size;
 }
 return 1;
}
int cmp5_probe_video_snapshot(const char*root,const Cmp5MediaCandidate*c,
 Cmp5VideoMetadata*out,Cmp5CancelCheck cancel,void*context){
 if(out)memset(out,0,sizeof(*out));
 if(!root||!c||!out||c->media_type!=2u||c->bytes<16||
    c->bytes>CMP5_VIDEO_MAX_FILE)return 0;
 static const char prefix[]="/user/av_contents/video/";
 const char*path=c->preview_path;
 size_t n=0;
 while(n<sizeof(c->preview_path)&&path[n])n++;
 if(n>=sizeof(c->preview_path)||n<=sizeof(prefix)-1||
    memcmp(path,prefix,sizeof(prefix)-1)!=0||
    path[n-1]=='/')return 0;
 const char*ext=strrchr(path,'.');
 const int mp4=ext&&strcasecmp(ext,".mp4")==0;
 const int webm=ext&&strcasecmp(ext,".webm")==0;
 if(!mp4&&!webm)return 0;
 for(size_t i=0;i<n;i++){
  if(path[i]=='\\'||(i>0&&path[i]=='/'&&path[i-1]=='/'))return 0;
 }
 char relative[CMP5_PATH_MAX];
 memcpy(relative,path+1,n);
 relative[n-1]=0;
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return 0;
 char*save=NULL;
 int file_fd=-1,parent_fd=-1;
 const char*leaf=NULL;
 for(char*part=strtok_r(relative,"/",&save);part;){
  if(part[0]=='.'||strchr(part,'\\')){close(fd);return 0;}
  char*next=strtok_r(NULL,"/",&save);
  const int child=openat(fd,part,O_RDONLY|O_NOFOLLOW|
                        (next?O_DIRECTORY:O_NONBLOCK));
  if(next||child<0)close(fd);
  if(child<0)return 0;
  if(!next){parent_fd=fd;leaf=part;file_fd=child;}
  fd=child;
  part=next;
 }
 if(file_fd<0){close(fd);return 0;}
 struct stat before,after;
 if(fstat(file_fd,&before)!=0||!same_file(&before,c)){
  close(parent_fd);close(fd);return 0;
 }
 Mp4 p={0};
 p.fd=fd;p.cancel=cancel;p.context=context;
 Cmp5VideoMetadata webm_result={0};
 int parsed=mp4?boxes(&p,0,c->bytes,0):
   cmp5_parse_webm_metadata_fd(fd,c->bytes,&webm_result,cancel,context);
 struct stat named_again;
 int stable=fstat(fd,&after)==0&&same_file(&after,c)&&
      same_snapshot_stat(&before,&after)&&
      fstatat(parent_fd,leaf,&named_again,AT_SYMLINK_NOFOLLOW)==0&&
      same_snapshot_stat(&before,&named_again);
 close(parent_fd);
 close(fd);
 if(!parsed||!stable||(mp4&&
    (!p.found_movie||!p.found_duration||!p.found_video))||
    (cancel&&cancel(context)))return 0;
 if(webm){*out=webm_result;return 1;}
 out->duration_ms=p.duration_ms;
 out->width=p.width;out->height=p.height;
 out->boxes_visited=p.count;
 return 1;
}
