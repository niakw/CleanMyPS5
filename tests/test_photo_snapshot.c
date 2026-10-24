#define _DEFAULT_SOURCE
/* Host fixture: anchored photo read with the native scanner's exact
 * mtime/ctime candidate identity. No decoder, GPU, or console needed. */
#include "media_preview.h"
#include "media_snapshot.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc,char**argv){
 if(argc!=4)return 2;
 const char*root=argv[1],*absolute=argv[2],*relative=argv[3];
 const uint8_t signature[8]={137,80,78,71,13,10,26,10};
 struct stat statbuf;
 assert(stat(absolute,&statbuf)==0);
 Cmp5MediaCandidate candidate={0};
 candidate.media_type=1u;
 candidate.bytes=(uint64_t)statbuf.st_size;
 cmp5_snapshot_capture(&candidate,&statbuf);
 assert(strlen(relative)<sizeof(candidate.preview_path));
 strcpy(candidate.preview_path,relative);
 size_t length=0;
 uint8_t*contents=cmp5_read_photo_snapshot(root,&candidate,&length);
 assert(contents&&length==candidate.bytes);
 assert(length>=sizeof(signature)&&memcmp(contents,signature,sizeof(signature))==0);
 free(contents);
 Cmp5MediaCandidate stale=candidate;
 stale.snapshot_mtime_nsec=(stale.snapshot_mtime_nsec+1u)%1000000000u;
 length=123;
 assert(cmp5_read_photo_snapshot(root,&stale,&length)==NULL&&length==0);
 stale=candidate;
 stale.snapshot_ctime_nsec=(stale.snapshot_ctime_nsec+1u)%1000000000u;
 length=123;
 assert(cmp5_read_photo_snapshot(root,&stale,&length)==NULL&&length==0);
 stale=candidate;
 stale.snapshot_ctime++;
 assert(cmp5_read_photo_snapshot(root,&stale,&length)==NULL&&length==0);
 /* Same bytes under two hardlinks must never be accepted. */
 char alias[4096];
 const int n=snprintf(alias,sizeof(alias),"%s.hardlink",absolute);
 assert(n>0&&(size_t)n<sizeof(alias));
 assert(link(absolute,alias)==0);
 assert(cmp5_read_photo_snapshot(root,&candidate,&length)==NULL&&length==0);
 assert(unlink(alias)==0);
 puts("Native photo snapshot accepts current and rejects stale/hardlinked: PASS");
 return 0;
}
