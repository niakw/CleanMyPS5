/* Read-only decoder of the pinned E2AREQ05 AMPR request wire layout.
 * PS5 x86-64 and 64-bit Linux/macOS have the same C ABI here. The engine
 * remains the ultimate independent authority at POST /api/resume.
 * This reader NEVER writes, repairs or adopts historical requests. */
#ifndef CMP5_AMPR_CHECKPOINT_REQUEST_H
#define CMP5_AMPR_CHECKPOINT_REQUEST_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
/* This viewer is compiled into a strictly read-only native title;
 * do not depend on the engine's output-creation/path-mutation header. */
#if defined(__cplusplus)
#define CMP5_REQ_STATIC_ASSERT static_assert
#else
#define CMP5_REQ_STATIC_ASSERT _Static_assert
#endif
typedef struct {
 char magic[8];
 char source[512];
 char smp_title[16];
 char dest_base[256];
 char name[64];
 int level,workers;
 bool clean_leftovers,overwrite,remove_source,resume,verify,dedup;
 int cache_mode;
 uint64_t test_pause_after;
 bool force;
 uint8_t reserved[63];
} Cmp5AmprSavedRequest;
CMP5_REQ_STATIC_ASSERT(sizeof(Cmp5AmprSavedRequest)==952,
                        "AMPR request ABI no longer matches pinned engine");
CMP5_REQ_STATIC_ASSERT(offsetof(Cmp5AmprSavedRequest,force)==888,
                        "AMPR request flag offsets changed");
static inline bool cmp5_ampr_terminated(const char*name,size_t length){
 return name&&memchr(name,0,length)!=NULL;
}
/* This is a conservative read-only prefilter, not a filesystem
 * authorization. The packing worker independently checks EVERY path
 * component and the actual source identity before resuming. */
static inline bool cmp5_ampr_source_candidate(const char*path){
 if(!path||path[0]!='/'||strlen(path)>=192u||
    strstr(path,"..")||strstr(path,"//")||strchr(path,'\\'))return false;
 if(strncmp(path,"/data/homebrew/",15u)==0&&path[15])return true;
 if(strncmp(path,"/data/etaHEN/games/",19u)==0&&path[19])return true;
 if(strncmp(path,"/mnt/",5u)!=0)return false;
 const char*p=path+5;
 if(strncmp(p,"usb",3u)!=0&&strncmp(p,"ext",3u)!=0)return false;
 return p[3]>='0'&&p[3]<='7'&&p[4]=='/'&&p[5]!=0;
}
static inline bool cmp5_ampr_request_valid(
 const Cmp5AmprSavedRequest*r,const char*base,const char*title){
 if(!r||!base||!title)return false;
 if(memcmp(r->magic,"E2AREQ05",8)!=0 ||
    !cmp5_ampr_terminated(r->source,sizeof(r->source)) ||
    !cmp5_ampr_terminated(r->smp_title,sizeof(r->smp_title)) ||
    !cmp5_ampr_terminated(r->dest_base,sizeof(r->dest_base)) ||
    !cmp5_ampr_terminated(r->name,sizeof(r->name)) ||
    r->source[0]==0 || r->level<1 || r->level>12 ||
    r->workers<1 || r->workers>16 ||
    r->clean_leftovers||r->overwrite||r->remove_source||r->force||
    !r->verify||!r->dedup||r->cache_mode!=0||r->test_pause_after!=0||
    !cmp5_ampr_source_candidate(r->source) ||
    strcmp(r->dest_base,base)!=0 || strcmp(r->name,title)!=0)
  return false;
 /* Managed images are explicitly tied to a title; extracted game roots
  * have no SMP title and are checked again by the real PS5 worker. */
 if(r->smp_title[0]&&strcmp(r->smp_title,title)!=0)return false;
 /* PS5 normally writes zeroed reserved fields. Reject nonzero extensions
  * until a deliberate, reviewed version bump supports those semantics. */
 for(size_t i=0;i<sizeof(r->reserved);i++)
  if(r->reserved[i]!=0u)return false;
 return true;
}
static inline bool cmp5_ampr_read_request(int fd,const char*base,
                                          const char*title){
 if(fd<0||!base||!title)return false;
 Cmp5AmprSavedRequest request={0};
 size_t read_total=0;
 while(read_total<sizeof(request)){
  ssize_t n=pread(fd,(char*)&request+read_total,
                  sizeof(request)-read_total,(off_t)read_total);
  if(n<0&&errno==EINTR)continue;
  if(n<=0)return false;
  read_total+=(size_t)n;
 }
 return cmp5_ampr_request_valid(&request,base,title);
}
#undef CMP5_REQ_STATIC_ASSERT
#endif
