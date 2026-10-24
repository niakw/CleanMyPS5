#define _DEFAULT_SOURCE
/* Deterministic host-only autoload rename and subsecond mutation races.
 * The diagnostic reader never writes; only this fixture injects changes. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
static char watched[PATH_MAX],moved[PATH_MAX];
static int action,triggered;
static ssize_t diagnostic_race_read(int fd,void*buffer,size_t count){
 ssize_t received=read(fd,buffer,count);
 if(received>0&&action&&!triggered){
  struct stat opened,named;
  if(fstat(fd,&opened)==0&&stat(watched,&named)==0&&
     opened.st_dev==named.st_dev&&opened.st_ino==named.st_ino){
   if(action==1){
    assert(rename(watched,moved)==0);
   }else{
    struct timespec t[2];
    t[0].tv_sec=0;t[0].tv_nsec=UTIME_OMIT;
#if defined(__APPLE__)
    t[1]=opened.st_mtimespec;
#else
    t[1]=opened.st_mtim;
#endif
    t[1].tv_nsec=(t[1].tv_nsec+1L)%1000000000L;
    assert(utimensat(AT_FDCWD,watched,t,0)==0);
   }
   triggered=1;
  }
 }
 return received;
}
#define read diagnostic_race_read
#include "../src/native/diagnostics.c"
#undef read
static void write_text(const char*path){
 FILE*f=fopen(path,"wb");assert(f);
 assert(fputs("etaHEN.elf\n",f)>=0);
 assert(fclose(f)==0);
}
static Cmp5AutoloadStats inspect(const char*root){
 size_t n=0;Cmp5AutoloadStats stats={0};
 char*json=cmp5_diagnostics_with_stats(root,&n,&stats);
 assert(json&&n==strlen(json));
 assert(!strstr(json,"etaHEN.elf"));
 free(json);return stats;
}
int main(void){
 char root[]="/tmp/cmp5-autoload-race-XXXXXX";
 assert(mkdtemp(root));
 char data[PATH_MAX],pldmgr[PATH_MAX];
 assert(snprintf(data,sizeof(data),"%s/data",root)>0);
 assert(snprintf(pldmgr,sizeof(pldmgr),"%s/data/pldmgr",root)>0);
 assert(snprintf(watched,sizeof(watched),"%s/autoload.txt",pldmgr)>0);
 assert(snprintf(moved,sizeof(moved),"%s/autoload.old",pldmgr)>0);
 assert(mkdir(data,0700)==0&&mkdir(pldmgr,0700)==0);
 write_text(watched);
 action=0;
 Cmp5AutoloadStats s=inspect(root);
 assert(s.etahen_references==1&&s.unreadable==0);
 action=1;triggered=0;
 s=inspect(root);
 assert(triggered&&s.unreadable==1&&s.etahen_references==0);
 assert(rename(moved,watched)==0);
 action=2;triggered=0;
 s=inspect(root);
 assert(triggered&&s.unreadable==1&&s.etahen_references==0);
 action=0;
 s=inspect(root);
 assert(s.etahen_references==1&&s.unreadable==0);
 /* Embedded binary zero would truncate parsing to the misleading prefix.
  * It must produce an unreadable state, never "no reference". */
 FILE*binary=fopen(watched,"wb");assert(binary);
 static const char binary_text[]={'#','n','o','n','e','\\0',
   'e','t','a','H','E','N','.','e','l','f','\\n'};
 assert(fwrite(binary_text,1,sizeof(binary_text),binary)==sizeof(binary_text));
 assert(fclose(binary)==0);
 s=inspect(root);
 assert(s.unreadable==1&&s.etahen_references==0&&s.no_reference==0);
 assert(unlink(watched)==0);
 assert(rmdir(pldmgr)==0&&rmdir(data)==0&&rmdir(root)==0);
 puts("Autoload name replacement and nanosecond metadata races: PASS");
 return 0;
}
