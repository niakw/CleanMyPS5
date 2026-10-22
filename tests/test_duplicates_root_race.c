#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
static char mutation_target[4096];
static int mutate_during_read=0;
static ssize_t race_read(int fd,void*buffer,size_t count){
 ssize_t got=read(fd,buffer,count);
 if(got>0&&mutate_during_read){
  struct stat st;
  if(stat(mutation_target,&st)!=0||
     chmod(mutation_target,(st.st_mode&07777)^S_IXUSR)!=0){
   perror("fixture metadata mutation");exit(10);
  }
  mutate_during_read=0;
 }
 return got;
}
#define open race_open
#define read race_read
#include "../src/native/duplicates.c"
#undef read
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
 if(!valid)return 9;
 /* Deterministic TOCTOU: alter permissions/ctime after a full-sized read
  * while the comparator is open. Size, inode and whole-second mtime remain
  * unchanged, but the result must be PARTIAL, with no exact duplicate. */
 int n=snprintf(mutation_target,sizeof(mutation_target),
   "%s/user/av_contents/photo/NPXS99999/GAME/a.png",test_backup);
 if(n<=0||(size_t)n>=sizeof(mutation_target))return 11;
 mutate_during_read=1;
 Cmp5DuplicateStats raced={0};
 size_t raced_size=0;
 char*unsafe=cmp5_duplicates_with_stats(test_backup,&raced_size,&raced);
 if(!unsafe)return 12;
 const int rejected=!mutate_during_read&&raced.partial==1u&&
   raced.exact_groups==0u&&raced.matching_files==0u&&
   strstr(unsafe,"\"partial\":true")!=NULL;
 free(unsafe);
 return rejected?0:13;
}
