#define _DEFAULT_SOURCE
#include "duplicates.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define PATH_CAP 2048
#define ITEM_CAP 128
#define VISIT_CAP 8000
#define DEPTH_CAP 10
#define FILE_CAP (32u*1024u*1024u)
#define IO_CAP (128u*1024u*1024u)
#define JSON_CAP (512u*1024u)
#define MAX_MONOTONIC_NS (6000000000ULL)
typedef struct {char path[PATH_CAP];off_t size;int paired;} Item;
typedef struct {Item items[ITEM_CAP];size_t count,visits;int partial;uint64_t deadline;} Scan;
static uint64_t now_ns(void){
 struct timespec ts;
 if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0||ts.tv_sec<0)return 0;
 return (uint64_t)ts.tv_sec*1000000000ULL+(uint64_t)ts.tv_nsec;
}
static int expired(const Scan*s){
 uint64_t now=now_ns();return s->deadline && now && now>s->deadline;
}
typedef struct {char *data;size_t len;int error;} Json;
static int join(char*out,size_t cap,const char*dir,const char*base){
 size_t n=strlen(dir);int r=snprintf(out,cap,"%s%s%s",dir,n&&dir[n-1]=='/'?"":"/",base);
 return r>=0&&(size_t)r<cap;
}
static int ends(const char*name,const char*ext){
 size_t n=strlen(name),m=strlen(ext);if(n<m)return 0;
 for(size_t i=0;i<m;i++)if(tolower((unsigned char)name[n-m+i])!=(unsigned char)ext[i])return 0;
 return 1;
}
static int is_image(const char *name){
 return ends(name,".png")||ends(name,".jpg")||ends(name,".jpeg")||ends(name,".jxr");
}
static void walk(Scan*s,const char*dir,int depth){
 if(s->partial)return;
 struct stat st;
 if(lstat(dir,&st)||!S_ISDIR(st.st_mode)){s->partial=1;return;}
 DIR*d=opendir(dir);if(!d){s->partial=1;return;}
 struct dirent*entry;
 while((entry=readdir(d))!=NULL){
  if(strcmp(entry->d_name,".")==0||strcmp(entry->d_name,"..")==0)continue;
  if(++s->visits>VISIT_CAP){s->partial=1;break;}
  if((s->visits&63u)==0&&expired(s)){s->partial=1;break;}
  char path[PATH_CAP];
  if(!join(path,sizeof(path),dir,entry->d_name)){s->partial=1;continue;}
  struct stat st2;if(lstat(path,&st2)){s->partial=1;continue;}
  if(S_ISLNK(st2.st_mode))continue;
  if(S_ISDIR(st2.st_mode)){
   if(depth<DEPTH_CAP)walk(s,path,depth+1);
   else s->partial=1;
  }else if(S_ISREG(st2.st_mode)&&is_image(path)&&st2.st_size>0&&st2.st_size<=(off_t)FILE_CAP){
   if(s->count>=ITEM_CAP){s->partial=1;break;}
   Item *i=&s->items[s->count++];
   memcpy(i->path,path,strlen(path)+1);
   i->size=st2.st_size;
  }
  if(s->partial)break;
 }
 closedir(d);
}
static int cmp_items(const void*a,const void*b){
 const Item*x=(const Item*)a,*y=(const Item*)b;
 if(x->size<y->size)return -1;if(x->size>y->size)return 1;
 return strcmp(x->path,y->path);
}
static ssize_t read_full(int fd,unsigned char*buf,size_t count){
 size_t got=0;
 while(got<count){ssize_t n=read(fd,buf+got,count-got);
  if(n<0){if(errno==EINTR)continue;return -1;}
  if(n==0)break;got+=(size_t)n;
 }return (ssize_t)got;
}
// 1 = byte-identical, 0 = different, -1 = inconclusive. Adds actual transferred bytes to budget.
static int compare(const Item*a,const Item*b,size_t *budget,const Scan*s){
 int x=open(a->path,O_RDONLY|O_NOFOLLOW),y=-1;
 if(x<0)return -1;
 y=open(b->path,O_RDONLY|O_NOFOLLOW);
 if(y<0){close(x);return -1;}
 struct stat sa,sb;
 if(fstat(x,&sa)||fstat(y,&sb)||!S_ISREG(sa.st_mode)||!S_ISREG(sb.st_mode)||
    sa.st_size!=a->size||sb.st_size!=b->size){close(x);close(y);return -1;}
 unsigned char p[32768],q[32768];off_t rest=a->size;int result=1;
 while(rest>0){
  size_t n=rest<(off_t)sizeof(p)?(size_t)rest:sizeof(p);
  if(*budget+n*2>IO_CAP||expired(s)){result=-1;break;}
  ssize_t ax=read_full(x,p,n),by=read_full(y,q,n);
  if(ax!=(ssize_t)n||by!=(ssize_t)n){result=-1;break;}
  *budget+=n*2;
  if(memcmp(p,q,n)!=0){result=0;break;}
  rest-=(off_t)n;
 }
 struct stat afterA,afterB;
 if(fstat(x,&afterA)||fstat(y,&afterB)||
    sa.st_size!=afterA.st_size||sb.st_size!=afterB.st_size||
    sa.st_mtime!=afterA.st_mtime||sb.st_mtime!=afterB.st_mtime)result=-1;
 close(x);close(y);return result;
}
static void add(Json*b,const char*str){
 size_t n=strlen(str);
 if(b->error||n>=JSON_CAP-b->len){b->error=1;return;}
 memcpy(b->data+b->len,str,n+1);b->len+=n;
}
static void quote(Json*b,const char*s){
 add(b,"\"");
 for(const unsigned char*p=(const unsigned char*)s;*p;p++){
  char buf[8];
  if(*p=='"'||*p=='\\'){buf[0]='\\';buf[1]=(char)*p;buf[2]=0;add(b,buf);}
  else if(*p<32){snprintf(buf,sizeof(buf),"\\u%04x",(unsigned)*p);add(b,buf);}
  else{buf[0]=(char)*p;buf[1]=0;add(b,buf);}
 }
 add(b,"\"");
}
char *cmp5_duplicates(const char*root,size_t*length){
 if(length)*length=0;if(!root||!length)return NULL;
 struct stat st;if(lstat(root,&st)||!S_ISDIR(st.st_mode))return NULL;
 Scan*s=(Scan*)calloc(1,sizeof(Scan));if(!s)return NULL;
 uint64_t started=now_ns();
 if(started && started<=UINT64_MAX-MAX_MONOTONIC_NS)s->deadline=started+MAX_MONOTONIC_NS;
 // Verify every fixed parent is a directory, not a symlink.
 char dir[PATH_CAP],step[PATH_CAP];snprintf(dir,sizeof(dir),"%s",root);
 const char*parts[]={"user","av_contents","photo","NPXS40087"};
 int valid=1;
 for(size_t k=0;k<4;k++){
  if(!join(step,sizeof(step),dir,parts[k])||lstat(step,&st)||!S_ISDIR(st.st_mode)){valid=0;break;}
  memcpy(dir,step,strlen(step)+1);
 }
 if(valid)walk(s,dir,0);else s->partial=1;
 qsort(s->items,s->count,sizeof(Item),cmp_items);
 Json out={0};out.data=(char*)calloc(1,JSON_CAP);
 if(!out.data){free(s);return NULL;}
 add(&out,"{\"schemaVersion\":1,\"mode\":\"read-only\",\"algorithm\":\"byte-compare\",\"groups\":[");
 size_t budget=0;int groups=0;
 for(size_t i=0;i<s->count;i++){
  if(expired(s)){s->partial=1;break;}
  if(s->items[i].paired)continue;
  size_t matches[ITEM_CAP],nm=1;matches[0]=i;
  for(size_t j=i+1;j<s->count&&s->items[j].size==s->items[i].size;j++){
   if(s->items[j].paired)continue;
   if(budget>=IO_CAP||expired(s)){s->partial=1;break;}
   int result=compare(&s->items[i],&s->items[j],&budget,s);
   if(result==1){matches[nm++]=j;s->items[j].paired=1;}
   else if(result<0)s->partial=1;
  }
  if(nm>1){
   if(groups++)add(&out,",");
   char n[40];snprintf(n,sizeof(n),"%llu",(unsigned long long)s->items[i].size);
   add(&out,"{\"bytesEach\":");add(&out,n);add(&out,",\"paths\":[");
   for(size_t k=0;k<nm;k++){if(k)add(&out,",");quote(&out,s->items[matches[k]].path);}
   add(&out,"]}");
  }
 }
 char stats[256];
 snprintf(stats,sizeof(stats),"],\"filesConsidered\":%zu,\"visited\":%zu,\"comparedBytes\":%zu,\"partial\":%s,"
  "\"limits\":{\"maxImages\":128,\"maxFileBytes\":33554432,\"maxReadBytes\":134217728,\"softDurationSeconds\":6},"
  "\"warning\":\"Images uniquement. Analyse plafonnee, doublons hors echantillon possibles.\"}",
  s->count,s->visits,budget,s->partial?"true":"false");
 add(&out,stats);free(s);
 if(out.error){free(out.data);return NULL;}
 *length=out.len;return out.data;
}
