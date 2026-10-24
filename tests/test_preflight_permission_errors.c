/* Host-only deterministic EPERM regression in actual preflight code.
 * A permission denial is evidence of LIMITED VISIBILITY, not no files. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int denied_stage=0;
static int injected_open(const char*path,int flags,...){
 if(denied_stage==1){errno=EPERM;return -1;}
 return open(path,flags);
}
static int injected_fstatat(int fd,const char*name,struct stat*st,int flags){
 if(denied_stage==2&&!strcmp(name,"protected")){
  errno=EPERM;return -1;
 }
 return fstatat(fd,name,st,flags);
}
static int injected_openat(int fd,const char*name,int flags,...){
 if(denied_stage==3&&!strcmp(name,"protected")){
  errno=EPERM;return -1;
 }
 return openat(fd,name,flags);
}
#define open injected_open
#define openat injected_openat
#define fstatat injected_fstatat
#include "../src/native/preflight.c"
#undef open
#undef openat
#undef fstatat

static void expect_status(const PathStatus*found,const char*state,const char*reason){
 assert(!strcmp(found->status,state));
 assert(!strcmp(found->reason,reason));
 assert(found->writable_hint==0||!strcmp(state,"accessible"));
}
int main(void){
 char sandbox[]="/tmp/cmp5-preflight-XXXXXX";
 assert(mkdtemp(sandbox));
 char folder[256];
 assert(snprintf(folder,sizeof(folder),"%s/protected",sandbox)>0);
 assert(mkdir(folder,0700)==0);
 PathStatus result=probe(sandbox,"protected");
 expect_status(&result,"accessible","directory-opened");
 denied_stage=1;
 result=probe(sandbox,"protected");
 expect_status(&result,"denied","permission-denied");
 denied_stage=2;
 result=probe(sandbox,"protected");
 expect_status(&result,"denied","permission-denied");
 denied_stage=3;
 result=probe(sandbox,"protected");
 expect_status(&result,"denied","permission-denied");
 denied_stage=0;
 result=probe(sandbox,"missing");
 expect_status(&result,"absent","not-found");
 assert(rmdir(folder)==0);
 assert(rmdir(sandbox)==0);
 puts("Read-only preflight EPERM at root/stat/child: denied (not unknown): PASS");
 return 0;
}
