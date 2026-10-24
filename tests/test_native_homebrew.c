#include "homebrew.h"
/* Exercise correctness assertions in release-mode CI as well as sanitizer builds. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int cancelled(void*ctx){(void)ctx;return 1;}
int main(int argc,char**argv){
 if(argc!=2)return 2;
 Cmp5HomebrewStats stats={0};
 size_t length=0;
 char*report=cmp5_homebrew_with_stats(argv[1],&length,&stats,NULL,NULL);
 assert(report&&length>100);
 assert(strstr(report,"\"mode\":\"read-only\""));
 assert(stats.homebrew==1);
 assert(stats.plugins==1);
 assert(stats.installed==1);
 assert(stats.app_data==2);
 assert(stats.entries==5);
 /* No plugin/homebrew directory is counted twice via /data. The
  * protected config and saves directories remain excluded. */
 assert(stats.nominal_bytes==24u); /* includes ordinary config.json (2 B) */
 assert(strstr(report,"\"path\":\"/data/etaHEN/notice.txt\""));
 assert(!strstr(report,"\"path\":\"/data/homebrew\","));
 assert(!strstr(report,"\"path\":\"/data/etaHEN\","));
 assert(strstr(report,"\"risk\":\"protected\""));
 free(report);
 Cmp5HomebrewStats interrupted={0};
 report=cmp5_homebrew_with_stats(argv[1],&length,&interrupted,cancelled,NULL);
 assert(report);
 assert(interrupted.partial==1);
 assert(strstr(report,"\"partial\":true"));
 assert(interrupted.entries==0);
 free(report);
 puts("Native homebrew summary and cancellation: PASS");
 return 0;
}
