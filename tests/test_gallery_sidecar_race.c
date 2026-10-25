#define _DEFAULT_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char*sidecar;
static int tamper;
static int injected;
static int reject_open,short_reads,unexpected_eof,seen_sidecar_read;
static const char *swap_root,*swap_backup,*swap_replacement;
static int swap_armed,swapped;
static int intercepted_open(const char*path,int flags,...){
 int fd=open(path,flags);
 if(fd>=0&&swap_armed&&!swapped&&!strcmp(path,swap_root)){
  if(rename(swap_root,swap_backup)!=0||
     rename(swap_replacement,swap_root)!=0){
   perror("fixture swap");close(fd);exit(5);
  }
  swapped=1;
 }
 return fd;
}
static int intercepted_openat(int fd,const char*name,int flags,...){
 if(reject_open&&!strcmp(name,"sample.dat")){
  errno=EACCES;
  return -1;
 }
 return openat(fd,name,flags);
}
static ssize_t intercepted_read(int fd,void*buf,size_t size){
 struct stat current,known;
 const int is_sidecar=sidecar&&fstat(fd,&current)==0&&
   stat(sidecar,&known)==0&&
   current.st_dev==known.st_dev&&current.st_ino==known.st_ino;
 if(is_sidecar&&unexpected_eof&&seen_sidecar_read)return 0;
 if(is_sidecar&&short_reads){
  seen_sidecar_read=1;
  if(size>1u)size=1u;
 }
 ssize_t n=read(fd,buf,size);
 if(is_sidecar&&n>0)seen_sidecar_read=1;
 if(n>0&&tamper&&!injected){
  struct stat current,known;
  if(fstat(fd,&current)==0&&stat(sidecar,&known)==0&&
     current.st_dev==known.st_dev&&current.st_ino==known.st_ino){
   const mode_t next=(known.st_mode&07777)^S_IXUSR;
   if(chmod(sidecar,next)!=0){perror("fixture chmod");exit(4);}
   injected=1;
  }
 }
 return n;
}
#define read intercepted_read
#define open intercepted_open
#define openat intercepted_openat
#include "../src/native/inventory.c"
#undef openat
#undef open
#undef read
static void test_overflow_guard(void){
 Scan*sample=calloc(1,sizeof(*sample));
 assert(sample);
 sample->summary_only=1;
 sample->bytes=UINT64_MAX-4u;
 struct stat metadata={0};
 metadata.st_size=8;
 file(sample,"/data/TestHomebrew/example.bin",&metadata,"application");
 assert(sample->stopped&&sample->partial&&sample->rootPartial);
 assert(sample->bytes==UINT64_MAX-4u&&sample->items==0u);
 free(sample);
}
int main(int argc,char**argv){
 test_overflow_guard();
 if(argc==5&&!strcmp(argv[4],"swap")){
  swap_root=argv[1];swap_backup=argv[2];swap_replacement=argv[3];
  swap_armed=1;
  size_t size=0;
  Cmp5MediaReview*result=calloc(1,sizeof(*result));
  assert(result);
  char*json=cmp5_overview_with_review(swap_root,&size,result);
  assert(json&&swapped&&result->count==1&&result->partial==0);
  assert(!strcmp(result->entries[0].title_id,"ORIGINAL"));
  assert(strstr(json,"\"partial\":false")!=NULL);
  free(json);free(result);
  puts("Gallery inventory retained initial root after pathname replacement");
  return 0;
 }
 if(argc!=3)return 2;
 sidecar=argv[2];
 size_t n=0;
 Cmp5MediaReview*review=calloc(1,sizeof(*review));
 if(!review)return 3;
 char*stable=cmp5_overview_with_review(argv[1],&n,review);
 assert(stable&&n>100);
 assert(review->count==1&&review->partial==0);
 assert(strcmp(review->entries[0].game_name,"Synthetic Game")==0);
 free(stable);
 // A valid regular file can return one byte per read; the scanner must
 // reconstruct the whole title instead of accepting a truncated prefix.
 short_reads=1;seen_sidecar_read=0;
 char*split=cmp5_overview_with_review(argv[1],&n,review);
 assert(split&&seen_sidecar_read&&review->count==1&&review->partial==0);
 assert(strcmp(review->entries[0].game_name,"Synthetic Game")==0);
 free(split);
 short_reads=0;seen_sidecar_read=0;
 // An advertised sidecar rejected by the OS cannot be silently omitted.
 reject_open=1;
 char*denied=cmp5_overview_with_review(argv[1],&n,review);
 assert(denied&&review->partial==1&&review->count==1);
 assert(review->entries[0].game_name[0]==0);
 assert(strstr(denied,"\"partial\":true"));
 assert(strstr(denied,"\"cleanupValidated\":false"));
 free(denied);reject_open=0;
 // Simulate EOF after one byte, without changing the disk snapshot.
 short_reads=1;unexpected_eof=1;
 char*incomplete=cmp5_overview_with_review(argv[1],&n,review);
 assert(incomplete&&review->partial==1&&seen_sidecar_read);
 assert(review->entries[0].game_name[0]==0);
 free(incomplete);
 short_reads=0;unexpected_eof=0;seen_sidecar_read=0;
 tamper=1;injected=0;
 char*changed=cmp5_overview_with_review(argv[1],&n,review);
 assert(changed&&n>100&&injected);
 assert(review->partial==1&&strstr(changed,"\"partial\":true"));
 assert(review->count==0||review->entries[0].game_name[0]==0);
 assert(strstr(changed,"\"cleanupValidated\":false"));
 free(changed);free(review);
 puts("PS5 Gallery sidecar denied / short read / premature EOF / in-place race: blocked");
 return 0;
}
