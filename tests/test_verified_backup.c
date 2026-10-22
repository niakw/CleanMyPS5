#define _DEFAULT_SOURCE
#include "verified_backup.h"
#include "sha256.h"
/* Test invariants must run even when CMake uses -DNDEBUG (Release). */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
static int always_cancel(void*context){(void)context;return 1;}
static int cancel_mid_copy(void*context){
 unsigned*count=(unsigned*)context;
 return ++(*count)>3u;
}
int main(int argc,char**argv){
 if(argc<5)return 2;
 const char*root=argv[1],*original=argv[2],*backup=argv[3],*restore=argv[4];
 const char*relative="/user/av_contents/photo/NPXS00000/GAME123/CAPTURE.JPG";
 Cmp5MediaCandidate media={0};struct stat st;
 assert(stat(original,&st)==0);
 media.media_type=1;media.bytes=(uint64_t)st.st_size;media.modified_at=st.st_mtime;
 media.snapshot_device=st.st_dev;media.snapshot_inode=st.st_ino;
 strcpy(media.preview_path,relative);
 Cmp5BackupReceipt a={0},b={0};
 assert(!cmp5_backup_media(root,&media,backup,"ONE",&a,always_cancel,NULL));
 assert(!cmp5_verify_backup(backup,"ONE",&a,NULL,NULL));
 assert(!cmp5_backup_media(root,&media,backup,"BAD/NAME",&a,NULL,NULL));
 unsigned checks=0;
 assert(!cmp5_backup_media(root,&media,backup,"ABORTED",&a,cancel_mid_copy,&checks));
 assert(checks>3u);
 char alias[512]{};
 (void)snprintf(alias,sizeof(alias),"%s.link",original);
 assert(link(original,alias)==0); // Test hardlinks are rejected, never unlinked.
 assert(!cmp5_backup_media(root,&media,backup,"HARDLINK",&a,NULL,NULL));
 assert(unlink(alias)==0);
 assert(cmp5_backup_media(root,&media,backup,"ONE",&a,NULL,NULL));
 assert(a.published&&a.bytes==media.bytes);
 assert(cmp5_verify_backup(backup,"ONE",&b,NULL,NULL));
 assert(b.bytes==a.bytes&&!memcmp(a.sha256,b.sha256,32));
 assert(!cmp5_backup_media(root,&media,backup,"ONE",&b,NULL,NULL));
 assert(!cmp5_restore_backup(backup,"ONE",restore,"SAVED",&b,always_cancel,NULL));
 assert(cmp5_restore_backup(backup,"ONE",restore,"SAVED",&b,NULL,NULL));
 assert(!cmp5_restore_backup(backup,"ONE",restore,"SAVED",&b,NULL,NULL));
 char path[512];snprintf(path,sizeof(path),"%s/%s",restore,"SAVED");
 FILE*f=fopen(path,"rb");assert(f);unsigned char buf[65536];size_t n;
 Cmp5Sha256 sha;cmp5_sha256_init(&sha);
 while((n=fread(buf,1,sizeof(buf),f))>0)cmp5_sha256_update(&sha,buf,n);
 assert(!ferror(f));fclose(f);unsigned char digest[32];cmp5_sha256_final(&sha,digest);
 assert(memcmp(a.sha256,digest,32)==0);
 for(unsigned i=0;i<32;i++)printf("%02x",digest[i]);
 puts("");
 if(argc==6){
   snprintf(path,sizeof(path),"%s/%s",backup,"ONE");
   f=fopen(path,"r+b");assert(f);assert(fseek(f,67,SEEK_SET)==0);
   int c=fgetc(f);assert(c>=0);assert(fseek(f,67,SEEK_SET)==0);
   assert(fputc(c^1,f)>=0);fclose(f);
   assert(!cmp5_verify_backup(backup,"ONE",&b,NULL,NULL));
   assert(!cmp5_restore_backup(backup,"ONE",restore,"ANOTHER",&b,NULL,NULL));
 }
 puts("Verified backup, no-clobber restore, cancellation and tamper: PASS");
 return 0;
}
