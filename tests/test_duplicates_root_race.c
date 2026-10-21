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

int main(int argc,char **argv){
 if(argc!=4)return 2;
 test_root=argv[1];test_backup=argv[2];test_replacement=argv[3];
 size_t length=0;char *json=cmp5_duplicates(test_root,&length);
 if(!json)return 4;
 if(!swapped||root_opens!=1){free(json);return 5;}
 int success=fwrite(json,1,length,stdout)==length;
 free(json);return success?0:6;
}
