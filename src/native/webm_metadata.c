#define _DEFAULT_SOURCE
#include "webm_metadata.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#define MAX_EBML_ITEMS 384u
#define MAX_EBML_READS (32u*1024u)

typedef struct {
 int fd;
 Cmp5CancelCheck cancel;
 void*context;
 uint32_t nodes,read_bytes;
 uint32_t width,height;
 uint64_t scale_ns;
 double duration;
 int found_header,found_doctype,found_segment,found_info,found_video;
} Ebml;
static int bytes_at(Ebml*p,uint64_t pos,uint8_t*out,size_t n){
 if(n>MAX_EBML_READS-p->read_bytes||pos>(uint64_t)INT64_MAX)return 0;
 if(p->cancel&&p->cancel(p->context))return 0;
 p->read_bytes+=(uint32_t)n;
 if(lseek(p->fd,(off_t)pos,SEEK_SET)<0)return 0;
 size_t done=0;
 while(done<n){
  if(p->cancel&&p->cancel(p->context))return 0;
  const ssize_t take=read(p->fd,out+done,n-done);
  if(take<0&&errno==EINTR)continue;
  if(take<=0)return 0;
  done+=(size_t)take;
 }
 return 1;
}
static int ebml_vint(Ebml*p,uint64_t pos,uint64_t end,int is_id,
 uint64_t*out,uint32_t*count,int*unknown){
 uint8_t first;
 if(pos>=end||!bytes_at(p,pos,&first,1)||first==0)return 0;
 unsigned length=1;
 uint8_t mark=0x80u;
 while(!(first&mark)&&length<8){length++;mark>>=1;}
 if(length>(is_id?4u:8u)||pos+length>end)return 0;
 uint64_t value=is_id?first:(uint64_t)(first&(mark-1u));
 for(unsigned i=1;i<length;i++){
  uint8_t byte;
  if(!bytes_at(p,pos+i,&byte,1))return 0;
  value=(value<<8)|byte;
 }
 *out=value;*count=length;
 if(unknown)*unknown=(!is_id&&value==((UINT64_C(1)<<(length*7u))-1u));
 return 1;
}
static int number(Ebml*p,uint64_t start,uint64_t len,uint64_t*out){
 if(!len||len>8)return 0;
 uint8_t data[8]={0};
 if(!bytes_at(p,start,data,(size_t)len))return 0;
 uint64_t n=0;
 for(unsigned i=0;i<len;i++)n=(n<<8)|data[i];
 *out=n;
 return 1;
}
static int webm_float(Ebml*p,uint64_t start,uint64_t len,double*out){
 uint8_t data[8]={0};
 if(len!=4&&len!=8)return 0;
 if(!bytes_at(p,start,data,(size_t)len))return 0;
 uint64_t packed=0;
 for(unsigned i=0;i<len;i++)packed=(packed<<8)|data[i];
 if(len==4){
  const uint32_t bits=(uint32_t)packed;
  float value;
  memcpy(&value,&bits,4);
  *out=value;
 }else{
  double value;
  memcpy(&value,&packed,8);
  *out=value;
 }
 // Reject infinities, NaNs and absurd timeline values.
 return *out>0.0&&*out<1000000000000000.0;
}
static int parse(Ebml*p,uint64_t start,uint64_t end,unsigned level){
 if(level>6||start>end)return 0;
 for(uint64_t pos=start;pos<end;){
  if(++p->nodes>MAX_EBML_ITEMS)return 0;
  uint64_t id=0,size=0;
  uint32_t id_len=0,size_len=0;
  int unknown=0;
  if(!ebml_vint(p,pos,end,1,&id,&id_len,NULL))return 0;
  if(!ebml_vint(p,pos+id_len,end,0,&size,&size_len,&unknown))return 0;
  const uint64_t body=pos+id_len+size_len;
  if(body>end)return 0;
  if(unknown){
   if(id!=0x18538067u||level!=0)return 0;
   size=end-body;
  }
  if(size>end-body)return 0;
  const uint64_t stop=body+size;
  if(level==0&&pos==0&&id!=0x1A45DFA3u)return 0;
  if(id==0x1A45DFA3u&&level==0){
   p->found_header=1;
   if(!parse(p,body,stop,6))return 0;
  }else if(id==0x4282u&&level==6){
   uint8_t doc[4]={0};
   if(size==4&&bytes_at(p,body,doc,4)&&memcmp(doc,"webm",4)==0)
    p->found_doctype=1;
  }else if(id==0x18538067u&&level==0){
   p->found_segment=1;
   if(!parse(p,body,stop,1))return 0;
  }else if(id==0x1549A966u&&level==1){
   p->found_info=1;
   if(!parse(p,body,stop,2))return 0;
  }else if(id==0x1654AE6Bu&&level==1){
   if(!parse(p,body,stop,3))return 0;
  }else if(id==0xAEu&&level==3){
   if(!parse(p,body,stop,4))return 0;
  }else if(id==0xE0u&&level==4){
   if(!parse(p,body,stop,5))return 0;
  }else if(id==0x2AD7B1u&&level==2){
   if(!number(p,body,size,&p->scale_ns)||p->scale_ns==0)return 0;
  }else if(id==0x4489u&&level==2){
   if(!webm_float(p,body,size,&p->duration))return 0;
  }else if((id==0xB0u||id==0xBAu)&&level==5){
   uint64_t value;
   if(!number(p,body,size,&value)||!value||value>16384u)return 0;
   if(id==0xB0u)p->width=(uint32_t)value;
   else p->height=(uint32_t)value;
   if(p->width&&p->height)p->found_video=1;
  }
  pos=stop;
 }
 return 1;
}
int cmp5_parse_webm_metadata_fd(int fd,uint64_t length,
 Cmp5VideoMetadata*out,Cmp5CancelCheck cancel,void*context){
 if(!out||length<16||length>(64ULL*1024ULL*1024ULL*1024ULL))return 0;
 Ebml p={0};p.fd=fd;p.cancel=cancel;p.context=context;p.scale_ns=1000000u;
 if(!parse(&p,0,length,0)||!p.found_header||!p.found_doctype||!p.found_segment||
    !p.found_info||!p.found_video||p.duration<=0.0||!p.scale_ns||
    (cancel&&cancel(context)))return 0;
 // Duration element units * TimestampScale (ns) => milliseconds.
 const double ms=p.duration*((double)p.scale_ns/1000000.0);
 if(!(ms>0.0)||ms>UINT64_C(2592000000))return 0;
 out->duration_ms=(uint64_t)(ms+0.5);
 out->width=p.width;out->height=p.height;
 out->boxes_visited=p.nodes;
 return out->duration_ms>0;
}
