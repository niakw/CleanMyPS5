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
 assert(stats.homebrew==2);
 assert(stats.plugins==1);
 assert(stats.installed==1);
 assert(stats.app_data==3);
 assert(stats.entries==7);
 assert(stats.partial==0u);
 assert(stats.data_root_state==1u&&stats.installed_root_state==1u);
 assert(strstr(report,"\"rootCoverage\":{\"appData\":\"readable\",\"installedTitles\":\"readable\"}"));
 /* No plugin/homebrew directory is counted twice via /data. The
  * protected config and saves directories remain excluded. */
 assert(stats.nominal_bytes==39u); /* separate unlinked app cache is nominal */
 assert(stats.top_count==4u);
 for(unsigned i=0;i<stats.top_count;i++){
  const Cmp5HomebrewTop*item=&stats.top[i];
  assert(item->category>=1u&&item->category<=4u);
  assert(item->label[0]&&strlen(item->label)<sizeof(item->label));
  assert(!strchr(item->label,'/'));
  assert(item->nominal_bytes>0);
  if(i)assert(stats.top[i-1u].nominal_bytes>=item->nominal_bytes);
 }
 assert(stats.top[0].nominal_bytes==19u);
 /* Cache-like names are just directory observations, not deletion claims. */
 assert(stats.cache_like_folders==4u&&stats.cache_like_files==5u);
 assert(stats.cache_like_nominal_bytes==22u&&!stats.cache_like_partial);
 assert(stats.cache_top_count==4u);
 assert(stats.cache_top[0].nominal_bytes==9u);
 assert(stats.cache_top[1].nominal_bytes==7u);
 assert(stats.cache_top[2].nominal_bytes==4u);
 assert(stats.cache_top[3].nominal_bytes==2u);
 assert(!strcmp(stats.cache_top[0].app_label,"ExampleTool"));
 assert(stats.cache_top[0].kind==1u&&stats.cache_top[0].files==2u);
 assert(stats.cache_top[0].name_matched==1u);
 assert(stats.cache_top[1].kind==2u&&stats.cache_top[1].age_hint==0u);
 assert(stats.cache_top[2].kind==3u&&stats.cache_top[2].age_hint==2u);
 assert(stats.cache_top[3].kind==1u&&
        !strcmp(stats.cache_top[3].app_label,"OtherApp")&&
        stats.cache_top[3].name_matched==0u);
 assert(stats.cache_like_name_matched==3u);
 assert(stats.cache_like_name_unmatched==1u);
 assert(!stats.homebrew_name_index_partial);
 /* 24 h recency is a negative signal only; old never means idle. */
 assert(stats.cache_mtime_recent==1u);
 assert(stats.cache_mtime_unknown==1u); /* future clock from .cache */
 assert(stats.cache_mtime_no_recent==2u);
 assert(stats.cache_mtime_recent+stats.cache_mtime_unknown+
        stats.cache_mtime_no_recent==stats.cache_like_folders);
 /* .cache is intentionally excluded from the general app subtotal,
  * and the independent cache-candidate subtotal is NON-ADDITIVE. */
 assert(stats.nominal_bytes==39u);
 assert(strstr(report,"\"cacheCandidates\":{\"folders\":4,\"files\":5,"));
 assert(strstr(report,"\"nominalBytes\":22,\"partial\":false,"));
 assert(strstr(report,"\"nameMatched\":3,\"noNameMatch\":1,\"nameIndexPartial\":false,"));
 assert(strstr(report,"\"recentMtime24h\":1,\"mtimeUnknown\":1,\"noRecentMtimeObserved\":2,\"idleVerified\":false,\"providerVerified\":false,\"deletable\":false"));
 assert(strstr(report,"\"path\":\"/data/etaHEN/notice.txt\""));
 assert(!strstr(report,"\"path\":\"/data/homebrew\","));
 assert(!strstr(report,"\"path\":\"/data/etaHEN\","));
 assert(strstr(report,"\"risk\":\"protected\""));
 free(report);
 /* JSON-only helper mode must produce exactly the same cache shortlist,
  * not silently show 0 just because a separate Stats struct is absent. */
 report=cmp5_homebrew(argv[1],&length);
 assert(report&&length>100u);
 assert(strstr(report,"\"cacheCandidates\":{\"folders\":4,\"files\":5,"));
 assert(strstr(report,"\"nominalBytes\":22,\"partial\":false,"));
 assert(strstr(report,"\"nameMatched\":3,\"noNameMatch\":1,"));
 assert(strstr(report,"\"recentMtime24h\":1,\"mtimeUnknown\":1,\"noRecentMtimeObserved\":2,"));
 assert(strstr(report,"\"idleVerified\":false,\"providerVerified\":false,\"deletable\":false"));
 assert(strstr(report,"\"deletable\":false"));
 free(report);
 Cmp5HomebrewStats interrupted={0};
 report=cmp5_homebrew_with_stats(argv[1],&length,&interrupted,cancelled,NULL);
 assert(report);
 assert(interrupted.partial==1);
 assert(strstr(report,"\"partial\":true"));
 assert(interrupted.entries==0);
 assert(interrupted.top_count==0&&interrupted.cache_top_count==0);
 assert(interrupted.cache_like_folders==0u&&
        interrupted.cache_like_nominal_bytes==0u);
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
