/* Deterministic post-unlink I/O failure: the real C production code
 * must NOT describe a removed private file as an untouched refusal.
 * No Sony paths and no device access; only a synthetic /tmp gallery. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "media_snapshot.h"

static int removed_saved=0,fail_post_remove=0,deny_remove=0;
static int test_fsync(int fd);
static int test_unlinkat(int fd,const char*name,int flags);
#define fsync test_fsync
#define unlinkat test_unlinkat
#include "../src/native/verified_backup.c"
#undef fsync
#undef unlinkat

static int test_fsync(int fd){
 if(removed_saved&&fail_post_remove){
  fail_post_remove=0;
  errno=EIO;
  return -1;
 }
 return fsync(fd);
}
static int test_unlinkat(int fd,const char*name,int flags){
 if(strcmp(name,"SAVED")==0&&deny_remove){
  errno=EPERM;
  return -1;
 }
 const int rc=unlinkat(fd,name,flags);
 if(rc==0&&strcmp(name,"SAVED")==0)removed_saved=1;
 return rc;
}
static void mkdir_one(const char*base,const char*name,char out[512]){
 const int n=snprintf(out,512,"%s/%s",base,name);
 assert(n>0&&n<512);
 assert(mkdir(out,0700)==0);
}
int main(void){
 char root[]="/tmp/cmp5-private-uncertain-XXXXXX";
 assert(mkdtemp(root)!=NULL);
 char user[512],av[512],contents[512],photo[512],np[512],game[512];
 char backup[512],rehearsal[512],original[512],raw[512];
 mkdir_one(root,"user",user);
 mkdir_one(user,"av_contents",av);
 mkdir_one(av,"photo",photo);
 mkdir_one(photo,"NPXS00000",np);
 mkdir_one(np,"GAME123",game);
 mkdir_one(root,"private-backups",backup);
 mkdir_one(root,"private-rehearsals",rehearsal);
 assert(snprintf(original,sizeof(original),"%s/CAPTURE.JPG",game)>0);
 int fd=open(original,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,0600);
 assert(fd>=0);
 unsigned char block[8192];
 for(size_t i=0;i<sizeof(block);i++)block[i]=(unsigned char)(i*19u+7u);
 assert(write(fd,block,sizeof(block))==(ssize_t)sizeof(block));
 assert(close(fd)==0);
 struct stat source;
 assert(stat(original,&source)==0);
 Cmp5MediaCandidate item={0};
 item.media_type=1u;
 item.bytes=(uint64_t)source.st_size;
 assert(snprintf(item.preview_path,sizeof(item.preview_path),
   "/user/av_contents/photo/NPXS00000/GAME123/CAPTURE.JPG")>0);
 cmp5_snapshot_capture(&item,&source);
 Cmp5BackupReceipt verified={0};
 assert(cmp5_backup_media(root,&item,backup,"ONE",&verified,NULL,NULL));
 assert(verified.published&&verified.bytes==sizeof(block));
 assert(cmp5_restore_backup(backup,"ONE",rehearsal,"SAVED",
                            &verified,NULL,NULL));
 assert(snprintf(raw,sizeof(raw),"%s/SAVED",rehearsal)>0);
 assert(access(raw,F_OK)==0);
 uint64_t nominal=999u;
 int attempted=99;
 /* A blocked unlink can leave the file intact yet the syscall attempt
  * is indeterminate to clients. Treat this as unsafe to claim no changes. */
 deny_remove=1;
 assert(!cmp5_discard_rehearsed_backup(backup,"ONE",rehearsal,"SAVED",
                                      &nominal,&attempted,NULL,NULL));
 assert(attempted==1&&!nominal&&access(raw,F_OK)==0);
 deny_remove=0;
 /* The raw file IS removed, but the directory fsync reports EIO.
  * In this case the caller MUST report an indeterminate mutation. */
 removed_saved=0;
 fail_post_remove=1;
 assert(!cmp5_discard_rehearsed_backup(backup,"ONE",rehearsal,"SAVED",
                                      &nominal,&attempted,NULL,NULL));
 assert(removed_saved&&attempted==1&&!nominal);
 assert(access(raw,F_OK)!=0);
 assert(cmp5_verify_backup(backup,"ONE",&verified,NULL,NULL));
 /* Subsequent attempts are blocked, with no new deletion attempt. */
 attempted=99;
 assert(!cmp5_discard_rehearsed_backup(backup,"ONE",rehearsal,"SAVED",
                                      &nominal,&attempted,NULL,NULL));
 assert(!attempted&&!nominal);
 (void)contents;
 puts("Private cleanup post-unlink I/O failure is indeterminate: PASS");
 return 0;
}
