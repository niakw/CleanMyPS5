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
static int fake_cache_identity_mode,cache_name_lookups;
/* Inject an identity swap at either lookup edge of /data/<app>/cache.
 * Actual files remain untouched; this simulates concurrent renames
 * reproducibly on macOS, Linux and under sanitizers. */
static int race_fstatat(int parent,const char*name,struct stat*st,int flags){
 const int rc=fstatat(parent,name,st,flags);
 if(rc==0&&fake_cache_identity_mode&&strcmp(name,"cache")==0){
  ++cache_name_lookups;
  if((fake_cache_identity_mode==1&&cache_name_lookups==1)||
     (fake_cache_identity_mode==2&&cache_name_lookups==2))
   st->st_ino+=1;
 }
 return rc;
}
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
#define fstatat race_fstatat
#include "../src/native/homebrew.c"
#undef fstatat
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
 /* A cache name that resolves to a different inode after stat/open
  * must not contribute its nominal bytes to an allegedly complete scan. */
 for(int mode=1;mode<=2;mode++){
  fake_cache_identity_mode=mode;cache_name_lookups=0;
  memset(&stats,0,sizeof(stats));size=0;
  json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
  assert(json&&size>100&&stats.partial&&stats.cache_like_partial);
  assert(cache_name_lookups>=2);
  assert(stats.cache_like_folders==2u);
  assert(stats.cache_like_nominal_bytes==11u);
  assert(strstr(json,"\"cacheCandidates\":{\"folders\":2,"));
  assert(strstr(json,"\"providerVerified\":false,\"deletable\":false"));
  free(json);
 }
 puts("Homebrew readdir, closedir and cache identity races: PARTIAL; healthy apps retained");
 return 0;
}
