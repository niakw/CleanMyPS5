#define _DEFAULT_SOURCE
#include "verified_group.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int stop(void*context){(void)context;return 1;}
static int stop_during_copy(void*context){
 unsigned *calls=(unsigned*)context;
 return ++(*calls)>10u;
}
static void snapshot(Cmp5MediaCandidate*c,const char*absolute,
 const char*relative,unsigned type){
 struct stat st;assert(stat(absolute,&st)==0);
 memset(c,0,sizeof(*c));c->media_type=type;
 c->bytes=(uint64_t)st.st_size;
 c->modified_at=st.st_mtime>0?(int64_t)st.st_mtime:0;
 c->snapshot_device=(uint64_t)st.st_dev;
 c->snapshot_inode=(uint64_t)st.st_ino;
 assert(strlen(relative)<sizeof(c->preview_path));
 strcpy(c->preview_path,relative);
}
int main(int argc,char**argv){
 if(argc!=7)return 2;
 const char*root=argv[1],*jxr=argv[2],*jpg=argv[3],*dat=argv[4];
 const char*private=argv[5],*restored=argv[6];
 const char*folder="/user/av_contents/photo/NPXS00000/GAME123/REC_001";
 Cmp5MediaCandidate media[3];
 char relative[256];
 const char*paths[3]={jxr,jpg,dat};
 const char*exts[3]={".jxr",".jpg",".dat"};
 for(unsigned i=0;i<3;i++){
  (void)snprintf(relative,sizeof(relative),"%s%s",folder,exts[i]);
  snapshot(&media[i],paths[i],relative,i==2?3:1);
 }
 Cmp5GroupReceipt a={0},b={0};
 assert(!cmp5_backup_group(root,media,3,private,"ALBUM",&a,stop,NULL));
 assert(!cmp5_verify_group(private,"ALBUM",&a,NULL,NULL));
 unsigned calls=0;
 assert(!cmp5_backup_group(root,media,3,private,"ABORTED",&a,
  stop_during_copy,&calls));
 assert(calls>10u);
 assert(!cmp5_verify_group(private,"ABORTED",&a,NULL,NULL));
 assert(cmp5_backup_group(root,media,3,private,"ALBUM",&a,NULL,NULL));
 assert(a.count==3&&a.bytes==media[0].bytes+media[1].bytes+media[2].bytes);
 assert(cmp5_verify_group(private,"ALBUM",&b,NULL,NULL));
 assert(!memcmp(a.manifest_sha256,b.manifest_sha256,32));
 assert(!cmp5_backup_group(root,media,3,private,"ALBUM",&b,NULL,NULL));
 assert(!cmp5_restore_group(private,"ALBUM",restored,&b,stop,NULL));
 assert(!cmp5_verify_restored_group(restored,"ALBUM",&b,NULL,NULL));
 assert(cmp5_restore_group(private,"ALBUM",restored,&b,NULL,NULL));
 assert(cmp5_verify_restored_group(restored,"ALBUM",&b,NULL,NULL));
 assert(b.count==3);
 assert(!cmp5_restore_group(private,"ALBUM",restored,&b,NULL,NULL));
 Cmp5MediaCandidate wrong[3];memcpy(wrong,media,sizeof(wrong));
 strcpy(wrong[1].preview_path,"/user/av_contents/photo/NPXS00000/GAME123/OTHER.jpg");
 assert(!cmp5_backup_group(root,wrong,3,private,"MIXED",&b,NULL,NULL));
 memcpy(wrong,media,sizeof(wrong));
 wrong[1].snapshot_device=wrong[0].snapshot_device;
 wrong[1].snapshot_inode=wrong[0].snapshot_inode;
 assert(!cmp5_backup_group(root,wrong,3,private,"ALIAS",&b,NULL,NULL));
 assert(!cmp5_backup_group(root,media,3,private,"BAD/ID",&b,NULL,NULL));
 assert(!cmp5_backup_group(root,media,1,private,"SINGLE",&b,NULL,NULL));
 assert(!cmp5_verify_group(private,"ALBUM__COMMIT",&b,NULL,NULL));
 char file[512];
 (void)snprintf(file,sizeof(file),"%s/ALBUM__2",restored);
 FILE*f=fopen(file,"r+b");assert(f);
 assert(fseek(f,13,SEEK_SET)==0);
 int original=fgetc(f);assert(original>=0);
 assert(fseek(f,13,SEEK_SET)==0);
 assert(fputc(original^0x20,f)>=0);
 assert(fclose(f)==0);
 assert(!cmp5_verify_restored_group(restored,"ALBUM",&b,NULL,NULL));
 (void)snprintf(file,sizeof(file),"%s/ALBUM__2",private);
 f=fopen(file,"r+b");assert(f);
 assert(fseek(f,68,SEEK_SET)==0);
 int original=fgetc(f);assert(original>=0);
 assert(fseek(f,68,SEEK_SET)==0);
 assert(fputc(original^0x80,f)>=0);
 assert(fclose(f)==0);
 assert(!cmp5_verify_group(private,"ALBUM",&b,NULL,NULL));
 assert(!cmp5_restore_group(private,"ALBUM",restored,&b,NULL,NULL));
 puts("Atomic multi-file capture group: backup, restore, no-clobber, rejection, tamper PASS");
 return 0;
}
