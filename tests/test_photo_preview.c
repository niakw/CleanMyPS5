#define _DEFAULT_SOURCE
#include "media_preview.h"
#include "preview_decode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char**argv){
 if(argc!=4)return 2;
 const char*root=argv[1],*absolute=argv[2],*relative=argv[3];
 struct stat st;
 assert(stat(absolute,&st)==0);
 Cmp5MediaCandidate snapshot={0};
 snapshot.media_type=1u;
 snapshot.bytes=(uint64_t)st.st_size;
 snapshot.modified_at=st.st_mtime>0?(int64_t)st.st_mtime:0;
 snapshot.snapshot_device=(uint64_t)st.st_dev;
 snapshot.snapshot_inode=(uint64_t)st.st_ino;
 assert(strlen(relative)<sizeof(snapshot.preview_path));
 strcpy(snapshot.preview_path,relative);
 size_t length=0;
 uint8_t*bytes=cmp5_read_photo_snapshot(root,&snapshot,&length);
 assert(bytes&&length==snapshot.bytes);
 Cmp5PreviewImage image={0};
 assert(cmp5_decode_preview(bytes,length,&image));
 assert(image.width>0&&image.height>0&&image.pixels);
 assert(image.width<=1100&&image.height<=620);
 cmp5_release_preview(&image);
 assert(image.pixels==NULL&&image.width==0);
 /* A gallery photo with multiple hardlinks cannot be treated as an
  * independently owned preview. The fixture must fail closed. */
 char alias[4096];
 int alias_length=snprintf(alias,sizeof(alias),"%s.alias",absolute);
 assert(alias_length>0&&(size_t)alias_length<sizeof(alias));
 assert(link(absolute,alias)==0);
 assert(!cmp5_read_photo_snapshot(root,&snapshot,&length)&&length==0u);
 assert(unlink(alias)==0);
 // Snapshot tampering, symlink traversal, and mismatched media kinds fail.
 snapshot.snapshot_inode++;
 assert(!cmp5_read_photo_snapshot(root,&snapshot,&length));
 snapshot.snapshot_inode--;
 snapshot.media_type=2u;
 assert(!cmp5_read_photo_snapshot(root,&snapshot,&length));
 snapshot.media_type=1u;
 snapshot.bytes=16u*1024u*1024u+1u;
 assert(!cmp5_read_photo_snapshot(root,&snapshot,&length));
 snapshot.bytes=(uint64_t)st.st_size;
 strcpy(snapshot.preview_path,"/user/av_contents/photo/../../data/private.png");
 assert(!cmp5_read_photo_snapshot(root,&snapshot,&length));
 assert(!cmp5_decode_preview((const uint8_t*)"garbage",7u,&image));
 free(bytes);
 puts("Native preview: secure anchored reading and bounded image decoding PASS");
 return 0;
}
