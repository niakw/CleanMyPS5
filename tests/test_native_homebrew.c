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
 if(argc!=4)return 2;
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
 assert(stats.partial==0u);
 assert(stats.data_root_state==1u&&stats.installed_root_state==1u);
 assert(strstr(report,"\"rootCoverage\":{\"appData\":\"readable\",\"installedTitles\":\"readable\"}"));
 /* No plugin/homebrew directory is counted twice via /data. The
  * protected config and saves directories remain excluded. */
 assert(stats.nominal_bytes==24u); /* includes ordinary config.json (2 B) */
 assert(stats.top_count==4u);
 for(unsigned i=0;i<stats.top_count;i++){
  const Cmp5HomebrewTop*item=&stats.top[i];
  assert(item->category>=1u&&item->category<=4u);
  assert(item->label[0]&&strlen(item->label)<sizeof(item->label));
  assert(!strchr(item->label,'/'));
  assert(item->nominal_bytes>0);
  if(i)assert(stats.top[i-1u].nominal_bytes>=item->nominal_bytes);
 }
 assert(stats.top[0].nominal_bytes==6u);
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
 assert(interrupted.top_count==0);
 free(report);
 Cmp5HomebrewStats missing_installed={0};
 report=cmp5_homebrew_with_stats(argv[2],&length,&missing_installed,NULL,NULL);
 assert(report&&missing_installed.entries==1u&&missing_installed.homebrew==1u);
 assert(missing_installed.data_root_state==1u);
 assert(missing_installed.installed_root_state==2u);
 assert(missing_installed.partial==1u);
 assert(strstr(report,"\"installedTitles\":\"absent\""));
 free(report);
 Cmp5HomebrewStats empty_mount={0};
 report=cmp5_homebrew_with_stats(argv[3],&length,&empty_mount,NULL,NULL);
 assert(report&&empty_mount.entries==0u&&empty_mount.partial==1u);
 assert(empty_mount.data_root_state==2u&&empty_mount.installed_root_state==2u);
 assert(strstr(report,"\"appData\":\"absent\""));
 free(report);
 puts("Native homebrew summary, mandatory root coverage and cancellation: PASS");
 return 0;
}
