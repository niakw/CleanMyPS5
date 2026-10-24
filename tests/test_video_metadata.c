#define _DEFAULT_SOURCE
#include "video_metadata.h"
#include "media_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static int cancellation(void*ignored){(void)ignored;return 1;}
/* Change metadata without changing size, inode or whole-second mtime.
 * A probe must reject it rather than publish a stale video result. */
typedef struct {
 const char*path;
 mode_t original_mode;
 int changed;
} ModeMutation;
static int mutate_mode_during_read(void*context){
 ModeMutation*state=(ModeMutation*)context;
 if(!state->changed){
  if(chmod(state->path,state->original_mode^S_IXUSR)!=0)return 1;
  state->changed=1;
 }
 return 0;
}
int main(int argc,char**argv){
 if(argc!=5&&argc!=6)return 2;
 const unsigned long long expected_ms=argc==6?strtoull(argv[5],NULL,10):95200ULL;
 const char*root=argv[1],*absolute=argv[2],*relative=argv[3];
 const int expected=strcmp(argv[4],"valid")==0;
 struct stat st={0};
 if(stat(absolute,&st)!=0)return 3;
 Cmp5MediaCandidate c={0};
 c.media_type=2;
 c.bytes=(uint64_t)st.st_size;
 cmp5_snapshot_capture(&c,&st);
 if(strlen(relative)>=sizeof(c.preview_path))return 4;
 strcpy(c.preview_path,relative);
 Cmp5VideoMetadata found={0};
 const int ok=cmp5_probe_video_snapshot(root,&c,&found,NULL,NULL);
 if(expected){
  assert(ok);
  assert(found.width==1920&&found.height==1080);
  assert(found.duration_ms==expected_ms);
  assert(found.boxes_visited>=4&&found.boxes_visited<=384);
  /* Reject changed media between inventory and metadata inspection,
   * even when inode, size and whole-second mtime are identical. */
  Cmp5MediaCandidate stale=c;
  stale.snapshot_mtime_nsec=(stale.snapshot_mtime_nsec+1u)%1000000000u;
  assert(!cmp5_probe_video_snapshot(root,&stale,&found,NULL,NULL));
  assert(found.duration_ms==0);
  stale=c;
  stale.snapshot_ctime_nsec=(stale.snapshot_ctime_nsec+1u)%1000000000u;
  assert(!cmp5_probe_video_snapshot(root,&stale,&found,NULL,NULL));
  assert(found.duration_ms==0);
  // Even successful metadata must be invalidated by inode mismatch.
  c.snapshot_inode++;
  assert(!cmp5_probe_video_snapshot(root,&c,&found,NULL,NULL));
  assert(found.width==0&&found.height==0);
  c.snapshot_inode--;
  ModeMutation changed={absolute,st.st_mode&0777,0};
  assert(!cmp5_probe_video_snapshot(root,&c,&found,
     mutate_mode_during_read,&changed));
  assert(changed.changed&&found.duration_ms==0);
  assert(chmod(absolute,changed.original_mode)==0);
  assert(!cmp5_probe_video_snapshot(root,&c,&found,cancellation,NULL));
  assert(found.duration_ms==0);
 }else{
  assert(!ok);
  assert(!found.width&&!found.height&&!found.duration_ms);
 }
 puts(expected?"MP4 duration and geometry PASS":"Unsafe/invalid MP4 rejected PASS");
 return 0;
}
