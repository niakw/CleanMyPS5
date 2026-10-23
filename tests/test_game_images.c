#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "game_images.h"
#include <assert.h>
#include <stdio.h>
int main(int argc,char**argv){
 if(argc!=2)return 2;
 Cmp5GameImageStats r={0};
 assert(cmp5_scan_game_images(argv[1],&r,NULL,NULL));
 assert(r.scanned_roots>=3);
 assert(r.count[CMP5_IMAGE_EXFAT]==2);
 assert(r.count[CMP5_IMAGE_FFPKG]==1);
 assert(r.count[CMP5_IMAGE_FFPFS]==1);
 assert(r.count[CMP5_IMAGE_FFPFSC]==1);
 assert(r.large_candidates==2);
 assert(r.folder_games==1);
 assert(r.large_folders==1);
 assert(r.folder_partials==0);
 assert(r.folder_nominal_bytes>=22ULL*1024*1024*1024);
 assert(r.hardlinks_skipped==2);
 assert(r.partial==1); /* A hardlink is never counted as freeable. */
 assert(r.nominal_bytes==54ULL*1024*1024*1024+340+560);
 printf("Known game image candidates (no content read): PASS\n");
 return 0;
}
