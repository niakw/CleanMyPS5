#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Inject a namespace replacement exactly after opening the root descriptor.
 * Including the real comparator keeps this deterministic on Linux and macOS,
 * without production hooks or timing-dependent race loops. */
static const char *test_root,*test_backup,*test_replacement;
static int root_opens,swapped;
static int race_open(const char *path,int flags,...){
 int fd=open(path,flags);
 if(fd>=0&&!strcmp(path,test_root)){
  root_opens++;
  if(!swapped){
   if(rename(test_root,test_backup)||rename(test_replacement,test_root)){
    perror("fixture rename");close(fd);exit(3);
   }
   swapped=1;
  }
 }
 return fd;
}
#define open race_open
#include "../src/native/duplicates.c"
#undef open

static int cancel_now(void*context){
 (void)context;return 1;
}
int main(int argc,char **argv){
 if(argc!=4)return 2;
 test_root=argv[1];test_backup=argv[2];test_replacement=argv[3];
 size_t length=0;Cmp5DuplicateStats stats={0};
 char *json=cmp5_duplicates_with_stats(test_root,&length,&stats);
 if(!json)return 4;
 if(stats.exact_groups!=1||stats.matching_files!=2||
    stats.files_considered!=2||stats.hardlinks_skipped!=0||
    stats.partial!=0||stats.duplicate_nominal_bytes!=sizeof("ORIGINAL CAPTURE")-1){
  free(json);return 7;
 }
 if(!swapped||root_opens!=1){free(json);return 5;}
 int success=fwrite(json,1,length,stdout)==length;
 free(json);
 if(!success)return 6;
 // New cancellable API must preserve the anchored-root invariant while
 // refusing to present an interrupted comparison as complete.
 Cmp5DuplicateStats interrupted={0};
 size_t interrupted_size=0;
 char*partial=cmp5_duplicates_with_stats_cancel(test_backup,&interrupted_size,
   &interrupted,cancel_now,NULL);
 if(!partial)return 8;
 int valid=interrupted.partial==1u&&interrupted.exact_groups==0u&&
   strstr(partial,"\"partial\":true")!=NULL;
 free(partial);
 return valid?0:9;
}
