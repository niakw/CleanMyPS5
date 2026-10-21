#define _DEFAULT_SOURCE
#include "inventory.h"
#include "diagnostics.h"
#include "assets.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
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
 const char*status=code==200?"OK":code==400?"Bad Request":code==404?"Not Found":code==405?"Method Not Allowed":"Internal Server Error";
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
static void handle(int fd,const char*root){
 struct timeval t={3,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof(t));
 char req[4096];ssize_t len=recv(fd,req,sizeof(req)-1,0);if(len<=0)return;req[len]=0;
 char method[12],path[256],proto[16];
 if(sscanf(req,"%11s %255s %15s",method,path,proto)!=3){plain(fd,400,"Bad request");return;}
 if(strcmp(method,"GET")!=0){plain(fd,405,"GET only");return;}
 if(strncmp(proto,"HTTP/1.",7)!=0){plain(fd,400,"Bad HTTP version");return;}
 if(strcmp(path,"/")==0||strcmp(path,"/index.html")==0)
  reply(fd,200,"text/html; charset=utf-8",cmp5_index,cmp5_index_len);
 else if(strcmp(path,"/styles.css")==0)
  reply(fd,200,"text/css; charset=utf-8",cmp5_styles,cmp5_styles_len);
 else if(strcmp(path,"/app.js")==0)
  reply(fd,200,"text/javascript; charset=utf-8",cmp5_script,cmp5_script_len);
 else if(strcmp(path,"/api/status")==0){
  #ifdef PS5
  static const char status[]="{\"app\":\"CleanMyPS5\",\"version\":\"0.2\",\"mode\":\"read-only\",\"target\":\"ps5\",\"api\":1}";
#else
  static const char status[]="{\"app\":\"CleanMyPS5\",\"version\":\"0.2\",\"mode\":\"read-only\",\"target\":\"host\",\"api\":1}";
#endif
  reply(fd,200,"application/json; charset=utf-8",status,sizeof(status)-1);
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
 int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return 1;
 int opt=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
 struct sockaddr_in bindaddr;memset(&bindaddr,0,sizeof(bindaddr));
 bindaddr.sin_family=AF_INET;bindaddr.sin_port=htons((uint16_t)port);bindaddr.sin_addr=ip;
 if(bind(fd,(struct sockaddr*)&bindaddr,sizeof(bindaddr))!=0||listen(fd,16)!=0){perror("bind/listen");close(fd);return 1;}
 printf("CleanMyPS5 http://%s:%d/ (read-only)\n",address,port);fflush(stdout);
 startup_notification();
 for(;;){int client=accept(fd,NULL,NULL);if(client<0){if(errno==EINTR)continue;break;}handle(client,root);close(client);}
 close(fd);return 0;
}
