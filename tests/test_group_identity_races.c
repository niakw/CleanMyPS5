/* Host-only deterministic mutation of a committed manifest and restored
 * member. Include the real implementation to intercept its read() calls. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static char watched[1024],moved[1024];
static int action,triggered;
static ssize_t race_read(int fd,void*out,size_t n){
 ssize_t got=read(fd,out,n);
 if(got>0&&action&&!triggered){
  struct stat opened,current;
  if(fstat(fd,&opened)==0&&stat(watched,&current)==0&&
     opened.st_dev==current.st_dev&&opened.st_ino==current.st_ino){
   if(action==1){
    if(rename(watched,moved)!=0){perror("rename race");exit(15);}
   }else if(action==2){
    if(chmod(watched,(current.st_mode&07777)^S_IXUSR)!=0){
     perror("chmod race");exit(16);
    }
   }else if(action==3){
    if(link(watched,moved)!=0){perror("hardlink race");exit(17);}
   }
   triggered=1;
  }
 }
 return got;
}
#define read race_read
#include "../src/native/verified_group.c"
#undef read
static void setup_watch(const char*parent,const char*name,const char*replacement,int operation){
 int length=snprintf(watched,sizeof(watched),"%s/%s",parent,name);
 assert(length>0&&(size_t)length<sizeof(watched));
 length=snprintf(moved,sizeof(moved),"%s/%s",parent,replacement);
 assert(length>0&&(size_t)length<sizeof(moved));
 action=operation;triggered=0;
}
static void write_member(int dir,const char*filename,const void*data,size_t len){
 int fd=openat(dir,filename,O_CREAT|O_WRONLY|O_EXCL,0600);
 assert(fd>=0);
 assert(write_exact(fd,(const uint8_t*)data,len));
 assert(close(fd)==0);
}
int main(int argc,char**argv){
 if(argc!=2)return 2;
 const char*workspace=argv[1];
 int dir=safe_dir(workspace);
 assert(dir>=0);
 const uint8_t one[]="SYNTHETIC_FIRST_MEMBER";
 const uint8_t two[]="SYNTHETIC_SECOND_MEMBER";
 const uint8_t *data[2]={one,two};
 const size_t lens[2]={sizeof(one)-1,sizeof(two)-1};
 Cmp5BackupReceipt receipts[2]={{0}};
 for(unsigned i=0;i<2;i++){
  receipts[i].bytes=lens[i];
  hash(data[i],lens[i],receipts[i].sha256);
  char name[48];member_name(name,"ALBUM",i);
  write_member(dir,name,data[i],lens[i]);
 }
 uint8_t manifest[MARK_SIZE];
 manifest_header(manifest,receipts,2);
 write_member(dir,"ALBUM__RESTORED",manifest,sizeof(manifest));
 uint8_t buffer[MARK_SIZE];
 Cmp5BackupReceipt found[CMP5_GROUP_MAX]={{0}};
 uint32_t count=0;
 Cmp5GroupReceipt result={0};
 assert(read_manifest(dir,"ALBUM",1,buffer,found,&count,&result));
 assert(count==2&&result.bytes==lens[0]+lens[1]);
 assert(raw_restored(dir,"ALBUM__1",lens[0],receipts[0].sha256,NULL,NULL));
 setup_watch(workspace,"ALBUM__RESTORED","MOVED__RESTORED",1);
 assert(!read_manifest(dir,"ALBUM",1,buffer,found,&count,&result));
 assert(triggered);
 assert(rename(moved,watched)==0);
 setup_watch(workspace,"ALBUM__RESTORED","UNUSED__RESTORED",2);
 assert(!read_manifest(dir,"ALBUM",1,buffer,found,&count,&result));
 assert(triggered);
 setup_watch(workspace,"ALBUM__RESTORED","LINKED__RESTORED",3);
 assert(!read_manifest(dir,"ALBUM",1,buffer,found,&count,&result));
 assert(triggered);
 assert(unlink(moved)==0);
 setup_watch(workspace,"ALBUM__1","MOVED__1",1);
 assert(!raw_restored(dir,"ALBUM__1",lens[0],receipts[0].sha256,NULL,NULL));
 assert(triggered);
 assert(rename(moved,watched)==0);
 setup_watch(workspace,"ALBUM__1","HARDLINK__1",3);
 assert(!raw_restored(dir,"ALBUM__1",lens[0],receipts[0].sha256,NULL,NULL));
 assert(triggered);
 assert(unlink(moved)==0);
 action=0;
 assert(raw_restored(dir,"ALBUM__1",lens[0],receipts[0].sha256,NULL,NULL));
 assert(close(dir)==0);
 puts("Private group manifest and restored media races refused: PASS");
 return 0;
}
