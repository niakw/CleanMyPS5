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
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "ampr_checkpoint_profile.h"
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
CMP5_REQ_STATIC_ASSERT(sizeof(bool)==1,
                        "AMPR request boolean layout changed");
static inline bool cmp5_ampr_terminated(const char*name,size_t length){
 return name&&memchr(name,0,length)!=NULL;
}
/* Mirror the pinned AMPR engine's lexical source allowlist, without
 * importing its filesystem mutation/create helpers into the native title.
 * THIS IS NOT path authorization: the worker independently re-opens the
 * selected source with no-follow descriptors at submission time. */
static inline bool cmp5_ampr_safe_component(const char*name){
 if(!name||name[0]=='.')return false;
 const size_t n=strlen(name);
 if(!n||n>=64u)return false;
 for(size_t i=0;i<n;i++){
  const unsigned char c=(unsigned char)name[i];
  if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
       (c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||
       c==' '||c=='['||c==']'||c=='('||c==')'))return false;
 }
 return true;
}
static inline bool cmp5_ampr_direct_child(const char*path,
                                          const char*parent){
 const size_t len=strlen(parent);
 return strncmp(path,parent,len)==0&&
        cmp5_ampr_safe_component(path+len);
}
static inline bool cmp5_ampr_image_suffix(const char*name){
 const char*dot=strrchr(name,'.');
 return dot&&(!strcasecmp(dot,".exfat")||!strcasecmp(dot,".ffpkg")||
              !strcasecmp(dot,".ffpfs")||!strcasecmp(dot,".ffpfsc"));
}
static inline bool cmp5_ampr_source_candidate(const char*path){
 if(!path||path[0]!='/'||strlen(path)>=192u)return false;
 if(cmp5_ampr_direct_child(path,"/data/homebrew/")||
    cmp5_ampr_direct_child(path,"/data/etaHEN/games/"))return true;
 if(strncmp(path,"/mnt/",5u)!=0)return false;
 const char*p=path+5;
 if(strncmp(p,"usb",3u)!=0&&strncmp(p,"ext",3u)!=0)return false;
 p+=3;
 if(p[0]<'0'||p[0]>'7'||p[1]!='/')return false;
 p+=2;
 return cmp5_ampr_direct_child(p,"homebrew/")||
        cmp5_ampr_direct_child(p,"etaHEN/games/")||
        (cmp5_ampr_safe_component(p)&&cmp5_ampr_image_suffix(p));
}
/* AMPR's serialized C bool has exactly one byte. Raw request.bin input
 * may contain 0x02 or 0xff: reading such an invalid bool through a C++
 * field can trigger undefined behaviour. Validate raw object bytes first,
 * before ANY typed boolean access (including short-circuit expressions). */
static inline bool cmp5_ampr_boolean_encoding_valid(
 const Cmp5AmprSavedRequest*r){
 if(!r)return false;
 const unsigned char*bytes=(const unsigned char*)r;
 static const size_t offsets[]={
  offsetof(Cmp5AmprSavedRequest,clean_leftovers),
  offsetof(Cmp5AmprSavedRequest,overwrite),
  offsetof(Cmp5AmprSavedRequest,remove_source),
  offsetof(Cmp5AmprSavedRequest,resume),
  offsetof(Cmp5AmprSavedRequest,verify),
  offsetof(Cmp5AmprSavedRequest,dedup),
  offsetof(Cmp5AmprSavedRequest,force)
 };
 for(size_t i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++)
  if(bytes[offsets[i]]>1u)return false;
 return true;
}
static inline bool cmp5_ampr_request_valid(
 const Cmp5AmprSavedRequest*r,const char*base,const char*title){
 if(!r||!base||!title||!cmp5_ampr_boolean_encoding_valid(r))return false;
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
 /* A checkpoint is resumable only if the worker persisted the exact
  * profile plan (or an explicit "no profile" marker). Legacy zeroed
  * extension fields cannot prove this and are not offered for automatic
  * resume; the old partial output is left untouched for inspection. */
 return cmp5_profile_pin_valid(r->reserved);
}
static inline bool cmp5_ampr_read_request(int fd,const char*base,
                                          const char*title){
 if(fd<0||!base||!title)return false;
 Cmp5AmprSavedRequest request;
 memset(&request,0,sizeof(request));
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
