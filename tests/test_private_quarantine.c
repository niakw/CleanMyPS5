/* Private laboratory rehearsal only. No system or Gallery writes. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "private_quarantine.h"
#include "verified_backup.h"
#include "media_snapshot.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void equal(const char*a,const char*b){
 FILE*left=fopen(a,"rb"),*right=fopen(b,"rb");
 assert(left&&right);
 unsigned char x[1000],y[1000];
 for(;;){
  size_t n=fread(x,1,sizeof(x),left);
  size_t m=fread(y,1,sizeof(y),right);
  assert(n==m&&memcmp(x,y,n)==0);
  if(!n)break;
 }
 assert(fclose(left)==0&&fclose(right)==0);
}
int main(int argc,char**argv){
 if(argc!=7)return 2;
 const char*root=argv[1],*gallery=argv[2],*lab=argv[3],
   *backup=argv[4],*trial=argv[5],*unsafe=argv[6];
 Cmp5MediaCandidate snapshot={0};
 struct stat st={0};
 assert(stat(gallery,&st)==0);
 snapshot.media_type=1u;snapshot.bytes=(uint64_t)st.st_size;
 strcpy(snapshot.preview_path,
   "/user/av_contents/photo/NPXS00000/GAME123/DEMO.JPG");
 cmp5_snapshot_capture(&snapshot,&st);
 Cmp5BackupReceipt receipt={0};
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_SOURCE_ONLY);
 assert(cmp5_private_lab_state(unsafe,"ITEM")==CMP5_LAB_INVALID);
 assert(!cmp5_private_lab_hold(lab,"ITEM",backup,"ONE",trial,"TRIAL"));
 assert(cmp5_backup_media(root,&snapshot,backup,"ONE",&receipt,NULL,NULL));
 assert(receipt.published&&receipt.bytes>0);
 assert(!cmp5_private_lab_hold(unsafe,"ITEM",backup,"ONE",trial,"TRIAL"));
 assert(!cmp5_private_lab_hold(lab,"../ITEM",backup,"ONE",trial,"TRIAL"));
 assert(cmp5_private_lab_hold(lab,"ITEM",backup,"ONE",trial,"TRIAL"));
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_HELD_ONLY);
 char held[1024],src[1024],rehearsed[1024];
 snprintf(src,sizeof(src),"%s/ITEM",lab);
 snprintf(held,sizeof(held),"%s/.ITEM.CMP5_HELD",lab);
 snprintf(rehearsed,sizeof(rehearsed),"%s/TRIAL",trial);
 assert(access(src,F_OK)!=0&&access(held,F_OK)==0);
 equal(gallery,held);equal(gallery,rehearsed);
 assert(!cmp5_private_lab_hold(lab,"ITEM",backup,"ONE",trial,"TRIAL"));
 assert(cmp5_private_lab_recover(lab,"ITEM",backup,"ONE"));
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_SOURCE_ONLY);
 assert(access(held,F_OK)!=0);equal(gallery,src);
 assert(!cmp5_private_lab_recover(lab,"ITEM",backup,"ONE"));
 /* A changed source is not eligible to be quarantined just because
  * an old backup exists. */
 int altered=open(src,O_WRONLY|O_NOFOLLOW);
 assert(altered>=0);
 assert(write(altered,"X",1)==1);
 assert(close(altered)==0);
 assert(!cmp5_private_lab_hold(lab,"ITEM",backup,"ONE",trial,"SECOND"));
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_SOURCE_ONLY);
 /* No clobber, ever: a symlink masquerading as a held member causes
  * ambiguous state, so neither the source nor the symlink is changed. */
 char attacker[1024];
 snprintf(attacker,sizeof(attacker),"%s/.ITEM.CMP5_HELD",lab);
 assert(symlink(gallery,attacker)==0);
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_AMBIGUOUS);
 assert(!cmp5_private_lab_recover(lab,"ITEM",backup,"ONE"));
 assert(!cmp5_private_lab_hold(lab,"ITEM",backup,"ONE",trial,"SECOND"));
 assert(unlink(attacker)==0);
 assert(cmp5_private_lab_state(lab,"ITEM")==CMP5_LAB_SOURCE_ONLY);
 puts("Private lab backup, rehearsal, quarantine, recovery, tamper & alias: PASS");
 return 0;
}
