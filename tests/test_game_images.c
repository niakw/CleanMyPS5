#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "game_images.h"
#include "game_scan_identity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char**argv){
 if(argc<2||argc>3)return 2;
 Cmp5GameImageStats r={0};
 /* A same-second modification must be noticed. The fixture deliberately
  * changes nanoseconds without changing the integer-second timestamp. */
 struct stat a={0},b={0};
 a.st_mode=S_IFDIR|0755;
 a.st_dev=1;a.st_ino=8;a.st_nlink=2;a.st_size=4096;
 a.st_mtime=10;a.st_ctime=10;
#if defined(__APPLE__)
 a.st_mtimespec.tv_nsec=123;
 a.st_ctimespec.tv_nsec=456;
#else
 a.st_mtim.tv_nsec=123;
 a.st_ctim.tv_nsec=456;
#endif
 b=a;
 assert(cmp5_same_game_directory(&a,&b));
#if defined(__APPLE__)
 b.st_mtimespec.tv_nsec++;
#else
 b.st_mtim.tv_nsec++;
#endif
 assert(!cmp5_same_game_directory(&a,&b));
 b=a;
#if defined(__APPLE__)
 b.st_ctimespec.tv_nsec++;
#else
 b.st_ctim.tv_nsec++;
#endif
 assert(!cmp5_same_game_directory(&a,&b));
 b=a;b.st_ino++;
 assert(!cmp5_same_game_directory(&a,&b));
 b=a;b.st_mode=S_IFREG|0644;
 assert(!cmp5_same_game_directory(&a,&b));
 assert(cmp5_scan_game_images(argv[1],&r,NULL,NULL));
 assert(r.scanned_roots>=3);
 if(argc==3&&!strcmp(argv[2],"overflow")){
  assert(r.count[CMP5_IMAGE_EXFAT]==21);
  assert(r.shown_images==12&&r.hidden_images==12);
  assert(r.folder_games==16&&r.shown_folders==12&&r.hidden_folders==4);
  assert(r.partial);
  for(unsigned i=1;i<r.shown_images;i++){
   assert(r.image[i-1].nominal_bytes>=r.image[i].nominal_bytes);
   assert(r.image[i].format!=CMP5_IMAGE_FFPFSC);
  }
  for(unsigned i=1;i<r.shown_folders;i++)
   assert(r.folder[i-1].nominal_bytes>=r.folder[i].nominal_bytes);
  assert(r.image[0].nominal_bytes==24ULL*1024*1024*1024);
  assert(r.folder[0].nominal_bytes>=22ULL*1024*1024*1024);
  puts("Oversubscribed conversion shortlist ranks largest games: PASS");
  return 0;
 }
 assert(r.count[CMP5_IMAGE_EXFAT]==3);
 assert(r.count[CMP5_IMAGE_FFPKG]==1);
 assert(r.count[CMP5_IMAGE_FFPFS]==1);
 assert(r.count[CMP5_IMAGE_FFPFSC]==1);
 assert(r.shown_images==6);
 assert(r.hidden_images==0);
 assert(r.image[0].format==CMP5_IMAGE_EXFAT);
 assert(!strcmp(r.image[0].label,"EAGAME.exfat"));
 assert(!strcmp(r.image[0].path,"/mnt/usb1/etaHEN/games/EAGAME.exfat"));
 assert(r.image[0].nominal_bytes==24ULL*1024*1024*1024);
 assert(r.large_candidates==3);
 assert(r.folder_games==2);
 assert(r.large_folders==1);
 assert(r.folder_partials==0);
 assert(r.folder_nominal_bytes>=22ULL*1024*1024*1024);
 assert(r.shown_folders==2);
 assert(r.hidden_folders==0);
 assert(r.folder[0].size_complete==1);
 assert(r.folder[0].nominal_bytes>=22ULL*1024*1024*1024);
 assert(!strcmp(r.folder[0].label,"PPSA12345"));
 assert(!strcmp(r.folder[0].path,"/data/homebrew/PPSA12345"));
 assert(!strcmp(r.folder[1].label,"EXTRACTED.exfat"));
 assert(!strcmp(r.folder[1].path,"/mnt/usb0/homebrew/EXTRACTED.exfat"));
 assert(r.folder[1].size_complete==1);
 assert(r.folder[1].nominal_bytes>=3ULL*1024*1024*1024);
 /* EMPTY.ffpkg is a directory, not a 0-byte image or playable game. */
 for(unsigned i=0;i<r.shown_images;i++)
  assert(strcmp(r.image[i].label,"EMPTY.ffpkg")!=0);
 for(unsigned i=0;i<r.shown_folders;i++)
  assert(strcmp(r.folder[i].label,"EMPTY.ffpkg")!=0);
 assert(r.hardlinks_skipped==2);
 assert(r.partial==1); /* A hardlink is never counted as freeable. */
 assert(r.nominal_bytes==78ULL*1024*1024*1024+340+560);
 printf("Known game image candidates (no content read): PASS\n");
 return 0;
}
