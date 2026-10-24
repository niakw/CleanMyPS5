#define _DEFAULT_SOURCE
#include <dirent.h>
#include <errno.h>
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
static char renamed_target[4096];
/* 1=change ctime/mode in place; 2=rename during read without changing inode. */
static int mutation_mode=0;
static ssize_t race_read(int fd,void*buffer,size_t count){
 ssize_t got=read(fd,buffer,count);
 if(got>0&&mutation_mode){
  if(mutation_mode==1){
   struct stat st;
   if(stat(mutation_target,&st)!=0||
      chmod(mutation_target,(st.st_mode&07777)^S_IXUSR)!=0){
    perror("fixture metadata mutation");exit(10);
   }
  }else if(mutation_mode==2){
   if(rename(mutation_target,renamed_target)!=0){
    perror("fixture pathname mutation");exit(14);
   }
  }
  mutation_mode=0;
 }
 return got;
}
/* Make the FIRST natural directory EOF look like a real EIO failure.
 * The production iterator must preserve the discovered candidate pair
 * but never advertise its coverage as complete. */
static int inject_directory_error=0;
static int injected_directory_error=0;
static struct dirent* race_readdir(DIR*d){
 struct dirent*entry=readdir(d);
 if(!entry&&inject_directory_error&&!injected_directory_error){
  errno=EIO;
  injected_directory_error=1;
 }
 return entry;
}
#define open race_open
#define read race_read
#define readdir race_readdir
#include "../src/native/duplicates.c"
#undef readdir
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
 mutation_mode=1;
 Cmp5DuplicateStats raced={0};
 size_t raced_size=0;
 char*unsafe=cmp5_duplicates_with_stats(test_backup,&raced_size,&raced);
 if(!unsafe)return 12;
 const int rejected=!mutation_mode&&raced.partial==1u&&
   raced.exact_groups==0u&&raced.matching_files==0u&&
   strstr(unsafe,"\"partial\":true")!=NULL;
 free(unsafe);
 if(!rejected)return 13;
 /* Rename the first filename after its descriptor is read. The underlying
  * bytes still match, but the old filename no longer points to the image. */
 int next=snprintf(renamed_target,sizeof(renamed_target),
   "%s/user/av_contents/photo/NPXS99999/GAME/a-renamed.png",test_backup);
 if(next<=0||(size_t)next>=sizeof(renamed_target))return 15;
 mutation_mode=2;
 Cmp5DuplicateStats moved={0};
 size_t moved_size=0;
 char*lost=cmp5_duplicates_with_stats(test_backup,&moved_size,&moved);
 if(!lost)return 16;
 const int safe=!mutation_mode&&moved.partial==1u&&
   moved.exact_groups==0u&&moved.matching_files==0u&&
   strstr(lost,"\"partial\":true")!=NULL;
 free(lost);
 if(!safe)return 17;
 /* A real readdir EIO is not EOF. Existing matched images remain
  * useful, but the complete-coverage flag must be false. */
 inject_directory_error=1;injected_directory_error=0;
 Cmp5DuplicateStats io={0};
 size_t io_len=0;
 char*io_report=cmp5_duplicates_with_stats(test_backup,&io_len,&io);
 inject_directory_error=0;
 if(!io_report)return 23;
 const int io_safe=injected_directory_error&&io.partial==1u&&
   io.exact_groups==1u&&io.matching_files==2u&&
   strstr(io_report,"\"partial\":true")!=NULL;
 free(io_report);
 if(!io_safe)return 24;
 /* Files outside the bounded comparison are counted and cause a partial
  * result. Other small matches should still be returned as verified. */
 char huge[4096];
 int written=snprintf(huge,sizeof(huge),
   "%s/user/av_contents/photo/NPXS99999/GAME/huge.png",test_backup);
 if(written<=0||(size_t)written>=sizeof(huge))return 18;
 int oversized=open(huge,O_WRONLY|O_CREAT|O_EXCL,0600);
 if(oversized<0)return 19;
 int prepared=ftruncate(oversized,(off_t)FILE_CAP+1u)==0;
 close(oversized);
 if(!prepared)return 20;
 Cmp5DuplicateStats limited={0};
 size_t limited_size=0;
 char*result=cmp5_duplicates_with_stats(test_backup,&limited_size,&limited);
 if(!result)return 21;
 const int accurate=limited.partial==1u&&
   limited.oversized_skipped==1u&&limited.exact_groups==1u&&
   limited.matching_files==2u&&
   strstr(result,"\"oversizedImagesSkipped\":1")!=NULL&&
   strstr(result,"\"partial\":true")!=NULL;
 free(result);
 return accurate?0:22;
}
