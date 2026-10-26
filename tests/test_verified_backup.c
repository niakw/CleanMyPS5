#define _DEFAULT_SOURCE
#include "verified_backup.h"
#include "media_snapshot.h"
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
 media.media_type=1;media.bytes=(uint64_t)st.st_size;
 cmp5_snapshot_capture(&media,&st);
 strcpy(media.preview_path,relative);
 Cmp5BackupReceipt a={0},b={0};
 Cmp5MediaCandidate stale=media;
 stale.snapshot_mtime_nsec=(stale.snapshot_mtime_nsec+1u)%1000000000u;
 assert(!cmp5_backup_media(root,&stale,backup,"STALE_MTIME",&a,NULL,NULL));
 stale=media;stale.snapshot_ctime_nsec=(stale.snapshot_ctime_nsec+1u)%1000000000u;
 assert(!cmp5_backup_media(root,&stale,backup,"STALE_CTIME",&a,NULL,NULL));
 stale=media;stale.snapshot_ctime++;
 assert(!cmp5_backup_media(root,&stale,backup,"STALE_CTIME_S",&a,NULL,NULL));
 assert(!cmp5_backup_media(root,&media,backup,"ONE",&a,always_cancel,NULL));
 assert(!cmp5_verify_backup(backup,"ONE",&a,NULL,NULL));
 assert(!cmp5_backup_media(root,&media,backup,"BAD/NAME",&a,NULL,NULL));
 assert(chmod(backup,0755)==0);
 assert(!cmp5_backup_media(root,&media,backup,"PUBLIC",&a,NULL,NULL));
 assert(chmod(backup,0700)==0);
 if(argc>=7)assert(!cmp5_backup_media(root,&media,argv[6],"ALIAS_DIR",&a,NULL,NULL));
 const uint64_t correct_size=media.bytes;
 media.bytes=(uint64_t)CMP5_BACKUP_LIMIT+1u;
 assert(!cmp5_backup_media(root,&media,backup,"TOO_LARGE",&a,NULL,NULL));
 media.bytes=correct_size;
 unsigned checks=0;
 assert(!cmp5_backup_media(root,&media,backup,"ABORTED",&a,cancel_mid_copy,&checks));
 assert(checks>3u);
 char alias[512]={0};
 (void)snprintf(alias,sizeof(alias),"%s.link",original);
 assert(link(original,alias)==0); // Test hardlinks are rejected, never unlinked.
 assert(!cmp5_backup_media(root,&media,backup,"HARDLINK",&a,NULL,NULL));
 assert(unlink(alias)==0);
 /* Link/unlink changes ctime even though bytes, size and inode stay fixed.
  * Re-read the snapshot after this deliberate fixture mutation. */
 assert(stat(original,&st)==0);
 cmp5_snapshot_capture(&media,&st);
 assert(cmp5_backup_media(root,&media,backup,"ONE",&a,NULL,NULL));
 assert(a.published&&a.bytes==media.bytes);
 assert(cmp5_verify_backup(backup,"ONE",&b,NULL,NULL));
 assert(b.bytes==a.bytes&&!memcmp(a.sha256,b.sha256,32));
 char symlink_path[512];
 (void)snprintf(symlink_path,sizeof(symlink_path),"%s/SYMLINK",backup);
 assert(symlink("ONE",symlink_path)==0);
 assert(!cmp5_verify_backup(backup,"SYMLINK",&b,NULL,NULL));
 assert(unlink(symlink_path)==0);
 assert(!cmp5_backup_media(root,&media,backup,"ONE",&b,NULL,NULL));
 /* A correct SHA-256 is not sufficient if a private backup is exposed
  * to group/other readers. Neither verify nor restore may endorse it. */
 char capsule_path[512];
 assert(snprintf(capsule_path,sizeof(capsule_path),"%s/ONE",backup)>0);
 assert(chmod(capsule_path,0644)==0);
 assert(!cmp5_verify_backup(backup,"ONE",&b,NULL,NULL));
 assert(!b.published&&!b.bytes);
 assert(!cmp5_restore_backup(backup,"ONE",restore,"LEAKED",&b,NULL,NULL));
 char refused_path[512];
 assert(snprintf(refused_path,sizeof(refused_path),"%s/LEAKED",restore)>0);
 assert(access(refused_path,F_OK)!=0);
 assert(chmod(capsule_path,0600)==0);
 assert(cmp5_verify_backup(backup,"ONE",&b,NULL,NULL));
 assert(!cmp5_restore_backup(backup,"ONE",restore,"SAVED",&b,always_cancel,NULL));
 assert(cmp5_restore_backup(backup,"ONE",restore,"SAVED",&b,NULL,NULL));
 assert(!cmp5_restore_backup(backup,"ONE",restore,"SAVED",&b,NULL,NULL));
 /* Restart-style audit reopens both private names and independently
  * checks SHA-256 without allocating another copy or overwriting files. */
 Cmp5BackupReceipt pair={0};
 assert(cmp5_verify_rehearsed_backup(backup,"ONE",restore,"SAVED",
                                    &pair,NULL,NULL));
 assert(pair.published&&pair.bytes==a.bytes&&
        memcmp(pair.sha256,a.sha256,32u)==0);
 assert(!cmp5_verify_rehearsed_backup(backup,"ONE",restore,"SAVED",
                                     &pair,always_cancel,NULL));
 assert(!pair.published);
 assert(!cmp5_verify_rehearsed_backup(backup,"ONE",backup,"ONE",
                                     &pair,NULL,NULL));
 assert(!pair.published);
 char path[512];snprintf(path,sizeof(path),"%s/%s",restore,"SAVED");
 /* A raw file can be changed without changing its source capsule:
  * post-restart inspection must reject the mismatch, not recopy it. */
 FILE*probe=fopen(path,"r+b");assert(probe);
 assert(fseek(probe,3,SEEK_SET)==0);
 int original_byte=fgetc(probe);assert(original_byte>=0);
 assert(fseek(probe,3,SEEK_SET)==0);
 assert(fputc(original_byte^1,probe)>=0);
 assert(fclose(probe)==0);
 assert(!cmp5_verify_rehearsed_backup(backup,"ONE",restore,"SAVED",
                                     &pair,NULL,NULL));
 assert(!pair.published);
 probe=fopen(path,"r+b");assert(probe);
 assert(fseek(probe,3,SEEK_SET)==0);
 assert(fputc(original_byte,probe)>=0);
 assert(fclose(probe)==0);
 assert(cmp5_verify_rehearsed_backup(backup,"ONE",restore,"SAVED",
                                    &pair,NULL,NULL));
 /* An alternative filename that resolves to the same raw object via
  * symlink is still untrusted. */
 char raw_alias[512];
 assert(snprintf(raw_alias,sizeof(raw_alias),"%s/ALIAS",restore)>0);
 assert(symlink("SAVED",raw_alias)==0);
 assert(!cmp5_verify_rehearsed_backup(backup,"ONE",restore,"ALIAS",
                                     &pair,NULL,NULL));
 assert(!pair.published);
 assert(unlink(raw_alias)==0);
 FILE*f=fopen(path,"rb");assert(f);unsigned char buf[65536];size_t n;
 Cmp5Sha256 sha;cmp5_sha256_init(&sha);
 while((n=fread(buf,1,sizeof(buf),f))>0)cmp5_sha256_update(&sha,buf,n);
 assert(!ferror(f));fclose(f);unsigned char digest[32];cmp5_sha256_final(&sha,digest);
 assert(memcmp(a.sha256,digest,32)==0);
 for(unsigned i=0;i<32;i++)printf("%02x",digest[i]);
 puts("");
 /* Only the separate, independently verified RAW copy can be released.
  * Its original verified capsule must remain intact and re-openable.
  * Never claim nominal length as verified filesystem blocks freed. */
 uint64_t discarded=0u;
 assert(!cmp5_discard_rehearsed_backup(backup,"ONE",restore,"SAVED",
                                      &discarded,always_cancel,NULL));
 assert(!discarded&&access(path,F_OK)==0);
 assert(!cmp5_discard_rehearsed_backup(backup,"ONE",backup,"ONE",
                                      &discarded,NULL,NULL));
 assert(!discarded);
 assert(cmp5_discard_rehearsed_backup(backup,"ONE",restore,"SAVED",
                                     &discarded,NULL,NULL));
 assert(discarded==media.bytes&&access(path,F_OK)!=0);
 assert(cmp5_verify_backup(backup,"ONE",&b,NULL,NULL));
 assert(!cmp5_discard_rehearsed_backup(backup,"ONE",restore,"SAVED",
                                      &discarded,NULL,NULL));
 assert(!discarded);
 assert(cmp5_restore_backup(backup,"ONE",restore,"SAVED2",&b,NULL,NULL));
 char scratch[512]={0};
 assert(snprintf(scratch,sizeof(scratch),"%s/SAVED2",restore)>0);
 FILE*alter=fopen(scratch,"r+b");assert(alter);
 assert(fseek(alter,15,SEEK_SET)==0);
 int byte=fgetc(alter);assert(byte>=0);
 assert(fseek(alter,15,SEEK_SET)==0);
 assert(fputc(byte^1,alter)>=0&&fclose(alter)==0);
 assert(!cmp5_discard_rehearsed_backup(backup,"ONE",restore,"SAVED2",
                                      &discarded,NULL,NULL));
 assert(!discarded&&access(scratch,F_OK)==0);
 if(argc>=6){
   snprintf(path,sizeof(path),"%s/%s",backup,"ONE");
   f=fopen(path,"r+b");assert(f);assert(fseek(f,67,SEEK_SET)==0);
   int c=fgetc(f);assert(c>=0);assert(fseek(f,67,SEEK_SET)==0);
   assert(fputc(c^1,f)>=0);fclose(f);
   assert(!cmp5_verify_backup(backup,"ONE",&b,NULL,NULL));
   assert(!cmp5_verify_rehearsed_backup(backup,"ONE",restore,"SAVED",
                                       &pair,NULL,NULL));
   assert(!cmp5_restore_backup(backup,"ONE",restore,"ANOTHER",&b,NULL,NULL));
 }
 puts("Verified backup, no-clobber restore, cancellation and tamper: PASS");
 return 0;
}
