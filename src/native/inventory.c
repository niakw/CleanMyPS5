#define _DEFAULT_SOURCE
#include "inventory.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define PATH_CAP 2048
#define OUTPUT_CAP (8u*1024u*1024u)
#define VISIT_CAP 30000u
#define ITEM_CAP 10000u
#define DEPTH_CAP 12
typedef struct {char *p;size_t len,cap;int fail;} Buffer;
typedef struct {Buffer b;size_t visits,items;int stopped;char warnings[32][256];size_t nw;} Scan;
static void put(Buffer*b,const char*s,size_t n){
 if(b->fail||n>=OUTPUT_CAP-b->len){b->fail=1;return;}
 size_t want=b->len+n+1;
 if(want>b->cap){size_t cap=b->cap?b->cap:4096;while(cap<want && cap<OUTPUT_CAP)cap*=2;if(cap>OUTPUT_CAP)cap=OUTPUT_CAP;
  char*t=(char*)realloc(b->p,cap);if(!t||cap<want){b->fail=1;return;}b->p=t;b->cap=cap;}
 memcpy(b->p+b->len,s,n);b->len+=n;b->p[b->len]=0;
}
static void add(Buffer*b,const char*s){put(b,s,strlen(s));}
static void quote(Buffer*b,const char*s){
 add(b,"\"");
 for(const unsigned char*p=(const unsigned char*)s;*p;p++){
  const unsigned char c=*p;
  if(c=='"'||c=='\\'){char q[2]={'\\',(char)c};put(b,q,2);}
  else if(c<32){char q[7];snprintf(q,sizeof(q),"\\u%04x",(unsigned)c);put(b,q,6);}
  else put(b,(const char*)p,1);
 }
 add(b,"\"");
}
static int join(char*out,size_t cap,const char*parent,const char*child){
 const size_t n=strlen(parent);const int r=snprintf(out,cap,"%s%s%s",parent,n&&parent[n-1]=='/'?"":"/",child);
 return r>=0&&(size_t)r<cap;
}
static int ext(const char*path,const char*extension){
 size_t n=strlen(path),k=strlen(extension);if(n<k)return 0;
 for(size_t i=0;i<k;i++)if(tolower((unsigned char)path[n-k+i])!=extension[i])return 0;
 return 1;
}
static void warn(Scan*s,const char*message,const char*path){
 if(s->nw>=32)return;
 snprintf(s->warnings[s->nw++],256,"%s : %.170s",message,path);
}
static void file(Scan*s,const char*path,const struct stat*info,const char*kind){
 if(s->stopped||s->b.fail||info->st_size<0)return;
 const off_t size=info->st_size;
 const char *category=0,*risk=0,*reason=0;
 if((strcmp(kind,"photo")==0 && (ext(path,".jpg")||ext(path,".jpeg")||ext(path,".png")||ext(path,".jxr")))||
    (strcmp(kind,"video")==0 && (ext(path,".mp4")||ext(path,".webm")))){
  category="media";risk="review";reason="Capture personnelle : selection manuelle";
 }else if(strcmp(kind,"etahen")==0){
  if(ext(path,".log")){category="logs";risk="review";reason="Journal : sauvegarder avant suppression";}
  else if(ext(path,".elf")||ext(path,".ini")||ext(path,".json")||ext(path,".sprx")){
   category="residuals";risk="protected";reason="Composant etaHEN : utilite non determinee";
  }
 }
 if(!category)return;
 if(s->items>=ITEM_CAP){s->stopped=1;warn(s,"Nombre limite de fichiers",path);return;}
 char id[24],n[32];snprintf(id,sizeof(id),"%zu",s->items+1);
 snprintf(n,sizeof(n),"%llu",(unsigned long long)size);
 if(s->items)add(&s->b,",");
 add(&s->b,"{\"id\":");quote(&s->b,id);
 add(&s->b,",\"path\":");quote(&s->b,path);
 add(&s->b,",\"bytes\":");add(&s->b,n);
 add(&s->b,",\"category\":");quote(&s->b,category);
 add(&s->b,",\"risk\":");quote(&s->b,risk);
 add(&s->b,",\"reason\":");quote(&s->b,reason);
 if(strcmp(category,"media")==0){
   // URI is always canonical root-relative, not a host mount/FTP path.
   const char*prefix="/user/av_contents/";
   const char*canonical=NULL;
   for(const char*cursor=path;(cursor=strstr(cursor,prefix))!=NULL;cursor++)
      canonical=cursor;
   if(canonical){add(&s->b,",\"previewPath\":");quote(&s->b,canonical);}
 }
 if(strcmp(category,"media")==0){
   const char*mark=strstr(path,"/NPXS40087/");
   if(mark){
     mark+=strlen("/NPXS40087/");
     size_t len=0;
     while(mark[len] && mark[len]!='/' && len<24 && (isalnum((unsigned char)mark[len])||mark[len]=='_'))len++;
     if(mark[len]=='/' && len>0){
       char title[25];memcpy(title,mark,len);title[len]=0;
       add(&s->b,",\"titleId\":");quote(&s->b,title);
     }
   }
 }
 char epoch[32];snprintf(epoch,sizeof(epoch),"%lld",(long long)(info->st_mtime>0?info->st_mtime:0));
 add(&s->b,",\"modifiedAt\":");add(&s->b,epoch);
 add(&s->b,"}");
 s->items++;
}
static void walk(Scan*s,const char*path,const char*kind,int depth){
 if(s->stopped||s->b.fail)return;
 struct stat st;if(lstat(path,&st)||!S_ISDIR(st.st_mode)){warn(s,"Repertoire inaccessible",path);return;}
 DIR*d=opendir(path);if(!d){warn(s,"Acces refuse",path);return;}
 struct dirent*e;
 while((e=readdir(d))!=NULL){
  if(strcmp(e->d_name,".")==0||strcmp(e->d_name,"..")==0)continue;
  if(++s->visits>VISIT_CAP){s->stopped=1;warn(s,"Limite de parcours atteinte",path);break;}
  char child[PATH_CAP];if(!join(child,sizeof(child),path,e->d_name)){warn(s,"Nom trop long",path);continue;}
  struct stat st2;if(lstat(child,&st2))continue;
  if(S_ISLNK(st2.st_mode))continue;
  if(S_ISDIR(st2.st_mode)){
   if(depth<DEPTH_CAP)walk(s,child,kind,depth+1);
   else warn(s,"Profondeur maximale atteinte",child);
  }else if(S_ISREG(st2.st_mode))file(s,child,&st2,kind);
  if(s->stopped||s->b.fail)break;
 }
 closedir(d);
}
static void root_scan(Scan*s,const char*root,const char*rel,const char*kind){
 char copy[PATH_CAP],prefix[PATH_CAP],next[PATH_CAP],full[PATH_CAP];
 if(!join(full,sizeof(full),root,rel))return;
 snprintf(prefix,sizeof(prefix),"%s",root);snprintf(copy,sizeof(copy),"%s",rel);
 char*save=0;
 for(char*part=strtok_r(copy,"/",&save);part;part=strtok_r(NULL,"/",&save)){
  if(!join(next,sizeof(next),prefix,part)){warn(s,"Racine trop longue",rel);return;}
  struct stat st;if(lstat(next,&st)||!S_ISDIR(st.st_mode)){warn(s,"Racine absente ou symbolique",rel);return;}
  memcpy(prefix,next,strlen(next)+1);
 }
 walk(s,full,kind,0);
}
char *cmp5_report(const char*root,size_t*len){
 if(len)*len=0;if(!root||!len)return NULL;
 struct stat st;if(lstat(root,&st)||!S_ISDIR(st.st_mode))return NULL;
 Scan*s=(Scan*)calloc(1,sizeof(Scan));if(!s)return NULL;
 add(&s->b,"{\"schemaVersion\":1,\"source\":\"CleanMyPS5 (read-only)\",\"items\":[");
 root_scan(s,root,"user/av_contents/photo/NPXS40087","photo");
 if(!s->stopped)root_scan(s,root,"user/av_contents/video/NPXS40087","video");
 if(!s->stopped)root_scan(s,root,"data/etaHEN","etahen");
 add(&s->b,"],\"warnings\":[");
 for(size_t i=0;i<s->nw;i++){if(i)add(&s->b,",");quote(&s->b,s->warnings[i]);}
 char stats[256];
 snprintf(stats,sizeof(stats),"],\"stats\":{\"visited\":%zu,\"items\":%zu,\"partial\":%s}}",
   s->visits,s->items,(s->stopped||s->nw>0)?"true":"false");
 add(&s->b,stats);
 char*output=s->b.p;
 if(s->b.fail){free(output);output=NULL;}else *len=s->b.len;
 free(s);return output;
}
