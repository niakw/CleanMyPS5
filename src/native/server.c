#define _DEFAULT_SOURCE
#include "inventory.h"
#include "diagnostics.h"
#include "duplicates.h"
#include "media.h"
#include "volumes.h"
#include "video.h"
#include "assets.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
static volatile sig_atomic_t cmp5_shutdown=0;
static void shutdown_signal(int signo){(void)signo;cmp5_shutdown=1;}
#include <strings.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef PS5
// Notification only: never persist an icon, app entry or settings modification.
typedef struct { char reserved[45]; char message[3075]; } Cmp5Notify;
int sceKernelSendNotificationRequest(int, Cmp5Notify*, size_t, int);
static void startup_notification(void){
 Cmp5Notify info;memset(&info,0,sizeof(info));
 snprintf(info.message,sizeof(info.message),"CleanMyPS5: interface http://127.0.0.1:8877/ (lecture seule)");
 sceKernelSendNotificationRequest(0,&info,sizeof(info),0);
}
#else
static void startup_notification(void){}
#endif
static int send_all(int fd,const void*data,size_t n){
 const char*p=(const char*)data;
 while(n){ssize_t k=send(fd,p,n,0);if(k<=0)return 0;p+=k;n-=k;}return 1;
}
static void reply(int fd,int code,const char*type,const void*body,size_t size){
 const char*status=code==200?"OK":code==400?"Bad Request":code==404?"Not Found":code==403?"Forbidden":code==405?"Method Not Allowed":code==413?"Content Too Large":code==416?"Range Not Satisfiable":"Internal Server Error";
 char header[1024];
 int n=snprintf(header,sizeof(header),
   "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n"
   "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n"
   "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'\r\n\r\n",
   code,status,type,size);
 if(n<0||(size_t)n>=sizeof(header))return;
 if(send_all(fd,header,(size_t)n)&&size)send_all(fd,body,size);
}
static void plain(int fd,int code,const char*text){reply(fd,code,"text/plain; charset=utf-8",text,strlen(text));}
static int hex_digit(char c){
 if(c>='0'&&c<='9')return c-'0';
 if(c>='a'&&c<='f')return c-'a'+10;
 if(c>='A'&&c<='F')return c-'A'+10;
 return -1;
}
static int decode_file_path(const char *input,char *out,size_t cap){
 size_t i=0;
 if(!input||!input[0])return 0;
 for(const char *p=input;*p;p++){
  unsigned char c=(unsigned char)*p;
  if(c=='%'){
   if(!p[1]||!p[2])return 0;
   int h=hex_digit(p[1]),l=hex_digit(p[2]);
   if(h<0||l<0)return 0;
   c=(unsigned char)((h<<4)|l);p+=2;
  }else if(c=='&'||c=='#'||c=='?')return 0;
  if(c<32||c>=127||c=='%'||c=='\\')return 0;
  if(i+1>=cap)return 0;
  out[i++]=(char)c;
 }
 out[i]=0;return i>0;
}
static void media_response(int client,const char *root,const char*encoded){
 char relative[2049];
 if(!decode_file_path(encoded,relative,sizeof(relative))){plain(client,400,"Invalid preview path");return;}
 int file=-1;size_t size=0;const char*mime=NULL;
 int status=cmp5_open_preview(root,relative,&file,&size,&mime);
 if(status!=200){plain(client,status,status==413?"Image too large":"Image unavailable");return;}
 char head[768];
 int n=snprintf(head,sizeof(head),
    "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
    "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
    "Referrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'none'\r\n\r\n",
    mime,size);
 if(n>0&&(size_t)n<sizeof(head)&&send_all(client,head,(size_t)n)){
  unsigned char buf[32768];size_t remaining=size;
  while(remaining){
   size_t want=remaining<sizeof(buf)?remaining:sizeof(buf);
   ssize_t got=read(file,buf,want);
   if(got<=0||!send_all(client,buf,(size_t)got))break;
   remaining-=(size_t)got;
  }
 }
 close(file);
}
// One 1 MiB chunk per GET: even long captures cannot monopolize the server.
#define CMP5_VIDEO_CHUNK (1024ULL*1024ULL)
static int parse_uint64(const char **p,uint64_t *value){
 if(**p<'0'||**p>'9')return 0;
 uint64_t number=0;
 while(**p>='0'&&**p<='9'){
  unsigned digit=(unsigned)(**p-'0');
  if(number>(UINT64_MAX-digit)/10u)return 0;
  number=number*10u+digit;(*p)++;
 }
 *value=number;return 1;
}
static int requested_video_range(const char *request,uint64_t size,uint64_t *start,uint64_t *end){
 // A strict single byte range; reject multipart ranges, duplicate Range headers,
 // and noncanonical signed/overflow values.
 const char *line=strstr(request,"\r\n");
 if(!line||!size)return 0;
 line+=2;const char *value=NULL;size_t length=0;
 while(*line && strncmp(line,"\r\n",2)!=0){
  const char*last=strstr(line,"\r\n");if(!last)return 0;
  if(last-line>=6 && strncasecmp(line,"Range:",6)==0){
   if(value)return 0;value=line+6;length=(size_t)(last-value);
  }
  line=last+2;
 }
 if(!value)return 0;
 while(length&&(*value==' '||*value=='\t')){value++;length--;}
 if(length<6||strncasecmp(value,"bytes=",6)!=0)return 0;
 const char *p=value+6,*limit=value+length;
 if(p>=limit)return 0;
 uint64_t a=0,b=0;
 if(*p=='-'){
  p++;if(!parse_uint64(&p,&b)||b==0||p!=limit)return 0;
  a=b>=size?0:size-b;b=size-1;
 }else{
  if(!parse_uint64(&p,&a)||p>=limit||*p++!='-')return 0;
  if(p==limit)b=size-1;
  else if(!parse_uint64(&p,&b)||p!=limit)return 0;
  if(a>=size||b<a)return 0;
  if(b>=size)b=size-1;
 }
 *start=a;
 uint64_t cap=a+CMP5_VIDEO_CHUNK-1;
 *end=b<cap?b:cap;
 return 1;
}
static void video_response(int client,const char*root,const char*encoded,
                           const char*request,int head_only){
 char relative[2049];
 if(!decode_file_path(encoded,relative,sizeof(relative))){plain(client,400,"Invalid video path");return;}
 int file=-1;uint64_t size=0;const char*mime=NULL;
 int status=cmp5_open_video(root,relative,&file,&size,&mime);
 if(status!=200){plain(client,status,status==413?"Video exceeds preview limit":"Video unavailable");return;}
 uint64_t start=0,end=size-1;
 if(!head_only&&!requested_video_range(request,size,&start,&end)){
  close(file);plain(client,416,"A single byte range is required for video preview");return;
 }
 uint64_t sendlen=head_only?size:end-start+1;
 char header[1024];int count;
 if(head_only){
  count=snprintf(header,sizeof(header),
     "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %llu\r\n"
     "Accept-Ranges: bytes\r\nConnection: close\r\nCache-Control: no-store\r\n"
     "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n\r\n",
     mime,(unsigned long long)size);
 }else{
  count=snprintf(header,sizeof(header),
     "HTTP/1.1 206 Partial Content\r\nContent-Type: %s\r\nContent-Length: %llu\r\n"
     "Content-Range: bytes %llu-%llu/%llu\r\n"
     "Accept-Ranges: bytes\r\nConnection: close\r\nCache-Control: no-store\r\n"
     "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n\r\n",
     mime,(unsigned long long)sendlen,
     (unsigned long long)start,(unsigned long long)end,(unsigned long long)size);
 }
 if(count>0&&(size_t)count<sizeof(header)&&send_all(client,header,(size_t)count)&&!head_only){
  unsigned char buffer[32768];uint64_t left=sendlen,offset=start;
  while(left){
   size_t want=left<sizeof(buffer)?(size_t)left:sizeof(buffer);
   ssize_t got=pread(file,buffer,want,(off_t)offset);
   if(got<0&&errno==EINTR)continue;
   if(got<=0||!send_all(client,buffer,(size_t)got))break;
   offset+=(uint64_t)got;left-=(uint64_t)got;
  }
 }
 close(file);
}

static int exact_header(const char *start,size_t length,const char *expected){
 return strlen(expected)==length&&memcmp(start,expected,length)==0;
}
static int validate_local_headers(const char*req,int port){
 const char*line=strstr(req,"\r\n");if(!line)return 0;
 line+=2;char host[80],local_host[80],origin[100],local_origin[100];
 snprintf(host,sizeof(host),"127.0.0.1:%d",port);
 snprintf(local_host,sizeof(local_host),"localhost:%d",port);
 snprintf(origin,sizeof(origin),"http://127.0.0.1:%d",port);
 snprintf(local_origin,sizeof(local_origin),"http://localhost:%d",port);
 int found=0;
 while(*line&&strncmp(line,"\r\n",2)!=0){
  const char*end=strstr(line,"\r\n");if(!end)return 0;
  const char*colon=memchr(line,':',(size_t)(end-line));
  if(!colon)return 0;
  const char*value=colon+1;
  while(value<end&&(*value==' '||*value=='\t'))value++;
  const char*last=end;
  while(last>value&&(last[-1]==' '||last[-1]=='\t'))last--;
  size_t len=(size_t)(last-value),name=(size_t)(colon-line);
  if(name==4&&strncasecmp(line,"Host",4)==0){
   if(found++||(!exact_header(value,len,host)&&!exact_header(value,len,local_host)))return 0;
  }else if(name==6&&strncasecmp(line,"Origin",6)==0){
   if(!exact_header(value,len,origin)&&!exact_header(value,len,local_origin))return 0;
  }else if(name==7&&strncasecmp(line,"Referer",7)==0){
   // A same-origin referrer may contain a path. Reject cross-origin URLs.
   if(!((len>=strlen(origin)&&!memcmp(value,origin,strlen(origin))&&
         (len==strlen(origin)||value[strlen(origin)]=='/'))||
        (len>=strlen(local_origin)&&!memcmp(value,local_origin,strlen(local_origin))&&
         (len==strlen(local_origin)||value[strlen(local_origin)]=='/'))))return 0;
  }else if(name==14&&strncasecmp(line,"Sec-Fetch-Site",14)==0){
   if(!exact_header(value,len,"same-origin")&&!exact_header(value,len,"none"))return 0;
  }
  line=end+2;
 }
 return found==1;
}

static void handle(int fd,const char*root,int port){
 struct timeval t={3,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof(t));
 char req[4096];size_t length=0;req[0]=0;
 while(length<sizeof(req)-1){
  ssize_t got=recv(fd,req+length,sizeof(req)-1-length,0);
  if(got<0&&errno==EINTR)continue;
  if(got<=0)return;
  length+=(size_t)got;req[length]=0;
  if(strstr(req,"\r\n\r\n"))break;
 }
 if(!strstr(req,"\r\n\r\n")){plain(fd,400,"Incomplete headers");return;}
 if(!validate_local_headers(req,port)){plain(fd,403,"Local origin required");return;}
 char method[12],path[3072],proto[16];
 if(sscanf(req,"%11s %3071s %15s",method,path,proto)!=3){plain(fd,400,"Bad request");return;}
 int is_head=strcmp(method,"HEAD")==0;
 if(strcmp(method,"GET")!=0&&!is_head){plain(fd,405,"GET and video HEAD only");return;}
 if(strncmp(proto,"HTTP/1.",7)!=0){plain(fd,400,"Bad HTTP version");return;}
 if(is_head && strncmp(path,"/api/video?file=",16)!=0){plain(fd,405,"HEAD is limited to video metadata");return;}
 if(strcmp(path,"/")==0||strcmp(path,"/index.html")==0)
  reply(fd,200,"text/html; charset=utf-8",cmp5_index,cmp5_index_len);
 else if(strcmp(path,"/styles.css")==0)
  reply(fd,200,"text/css; charset=utf-8",cmp5_styles,cmp5_styles_len);
 else if(strcmp(path,"/app.js")==0)
  reply(fd,200,"text/javascript; charset=utf-8",cmp5_script,cmp5_script_len);
 else if(strncmp(path,"/api/video?file=",16)==0){
  video_response(fd,root,path+16,req,is_head);
 }else if(strncmp(path,"/api/media?file=",16)==0){
  media_response(fd,root,path+16);
 }else if(strcmp(path,"/api/status")==0){
  #ifdef PS5
  static const char status[]="{\"app\":\"CleanMyPS5\",\"version\":\"0.2\",\"mode\":\"read-only\",\"target\":\"ps5\",\"api\":1}";
#else
  static const char status[]="{\"app\":\"CleanMyPS5\",\"version\":\"0.2\",\"mode\":\"read-only\",\"target\":\"host\",\"api\":1}";
#endif
  reply(fd,200,"application/json; charset=utf-8",status,sizeof(status)-1);
 }else if(strcmp(path,"/api/volumes")==0){
  size_t size=0;char*json=cmp5_volumes(root,&size);
  if(!json){plain(fd,500,"Mount metrics unavailable");return;}
  reply(fd,200,"application/json; charset=utf-8",json,size);free(json);
 }else if(strcmp(path,"/api/duplicates")==0){
  size_t size=0;char*json=cmp5_duplicates(root,&size);
  if(!json){plain(fd,500,"Duplicate analysis unavailable");return;}
  reply(fd,200,"application/json; charset=utf-8",json,size);free(json);
 }else if(strcmp(path,"/api/diagnostics")==0){
  size_t size=0;char*json=cmp5_diagnostics(root,&size);
  if(!json){plain(fd,500,"Diagnostic unavailable");return;}
  reply(fd,200,"application/json; charset=utf-8",json,size);free(json);
 }else if(strcmp(path,"/api/report")==0){
  size_t size=0;char*json=cmp5_report(root,&size);
  if(!json){plain(fd,500,"Scan unavailable");return;}
  reply(fd,200,"application/json; charset=utf-8",json,size);free(json);
 }else plain(fd,404,"Not found");
}
int main(int argc,char**argv){
#ifdef PS5
 const char*root="/";
#else
 const char*root=NULL;
#endif
 const char*address="127.0.0.1";int port=8877;
 for(int i=1;i<argc;i++){
  if(strcmp(argv[i],"--root")==0&&i+1<argc)root=argv[++i];
  else if(strcmp(argv[i],"--port")==0&&i+1<argc)port=atoi(argv[++i]);
  else if(strcmp(argv[i],"--listen")==0&&i+1<argc)address=argv[++i];
  else {fprintf(stderr,"Usage: cleanmyps5-server --root DIR [--port 8877] [--listen 127.0.0.1]\n");return 2;}
 }
 if(!root||port<1024||port>65535){fprintf(stderr,"Host requires root, port must be >=1024\n");return 2;}
 if(strcmp(address,"127.0.0.1")!=0){fprintf(stderr,"Network access disabled until paired/authenticated access is implemented\n");return 2;}
 struct in_addr ip;if(inet_pton(AF_INET,address,&ip)!=1)return 2;
 signal(SIGPIPE,SIG_IGN);
 signal(SIGTERM,shutdown_signal);
 signal(SIGINT,shutdown_signal);
 int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return 1;
 int opt=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
 struct sockaddr_in bindaddr;memset(&bindaddr,0,sizeof(bindaddr));
 bindaddr.sin_family=AF_INET;bindaddr.sin_port=htons((uint16_t)port);bindaddr.sin_addr=ip;
 if(bind(fd,(struct sockaddr*)&bindaddr,sizeof(bindaddr))!=0||listen(fd,16)!=0){perror("bind/listen");close(fd);return 1;}
 printf("CleanMyPS5 http://%s:%d/ (read-only)\n",address,port);fflush(stdout);
 startup_notification();
 while(!cmp5_shutdown){
  int client=accept(fd,NULL,NULL);
  if(client<0){
   if(errno==EINTR&&!cmp5_shutdown)continue;
   if(cmp5_shutdown)break;
   perror("accept");break;
  }
  handle(client,root,port);close(client);
 }
 close(fd);puts("CleanMyPS5 stopped, listener closed.");return 0;
}
