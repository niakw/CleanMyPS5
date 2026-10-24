#define _DEFAULT_SOURCE
#include "media_preview.h"
#include "media_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

int main(int argc,char**argv){
 if(argc!=5)return 2;
 const char*root=argv[1],*absolute=argv[2],*relative=argv[3];
 const int allowed=strcmp(argv[4],"valid")==0;
 struct stat st;
 assert(stat(absolute,&st)==0);
 Cmp5MediaCandidate c={0};
 c.media_type=1;
 c.bytes=(uint64_t)st.st_size;
 cmp5_snapshot_capture(&c,&st);
 assert(strlen(relative)<sizeof(c.preview_path));
 strcpy(c.preview_path,relative);
 size_t read_count=0;
 uint8_t*data=cmp5_read_photo_snapshot(root,&c,&read_count);
 if(allowed){
  assert(data);
  assert(read_count>=3);
  assert(data[0]==0xff&&data[1]==0xd8&&data[2]==0xff);
  assert(read_count!=c.bytes);
  free(data);
 }else{
  assert(data==NULL&&read_count==0);
 }
 puts(allowed?"Companion JPEG snapshot: PASS":"Unsafe/unavailable JPG twin rejected: PASS");
 return 0;
}
