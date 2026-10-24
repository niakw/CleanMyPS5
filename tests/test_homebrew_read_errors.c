/* Deterministic POSIX directory I/O error regression in the real,
 * read-only homebrew scanner. Host fixture only, never console files. */
#define _DEFAULT_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int eof_error_enabled,eof_error_seen;
static int close_error_enabled,close_error_seen;
static struct dirent* race_readdir(DIR*dir){
 struct dirent*entry=readdir(dir);
 if(!entry&&eof_error_enabled&&!eof_error_seen){
  eof_error_seen=1;errno=EIO;
 }
 return entry;
}
static int race_closedir(DIR*dir){
 const int rc=closedir(dir);
 if(rc==0&&close_error_enabled&&!close_error_seen){
  close_error_seen=1;errno=EIO;return -1;
 }
 return rc;
}
#define readdir race_readdir
#define closedir race_closedir
#include "../src/native/homebrew.c"
#undef closedir
#undef readdir
int main(int argc,char**argv){
 if(argc!=2)return 2;
 Cmp5HomebrewStats stats={0};
 size_t size=0;
 eof_error_enabled=1;
 char*json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
 assert(json&&size>100);
 assert(eof_error_seen&&stats.partial==1u&&stats.homebrew==1u);
 assert(stats.installed==1u&&stats.nominal_bytes>0u);
 assert(strstr(json,"\"partial\":true"));
 free(json);
 eof_error_enabled=0;close_error_enabled=1;close_error_seen=0;
 memset(&stats,0,sizeof(stats));size=0;
 json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
 assert(json&&size>100);
 assert(close_error_seen&&stats.partial==1u&&stats.homebrew==1u);
 assert(stats.installed==1u);
 assert(strstr(json,"\"partial\":true"));
 free(json);
 puts("Homebrew readdir and closedir failures: PARTIAL with healthy apps retained");
 return 0;
}
