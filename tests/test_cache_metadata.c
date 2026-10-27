/* Host fixture for the real, read-only metadata-only cache scanner. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "homebrew.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char**argv){
 if(argc!=2&&argc!=3)return 2;
 Cmp5HomebrewStats stats={0};
 size_t size=0;
 char*json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
 assert(json&&size>100u);
 free(json);
 if(argc==3&&strcmp(argv[2],"excluded")==0){
  assert(stats.cache_like_folders==1u&&stats.cache_ledger_count==1u);
  const Cmp5CacheLedger*item=&stats.cache_ledger[0];
  assert(!item->metadata_complete);
  assert((item->partial_reasons&CMP5_CACHE_REASON_EXCLUDED)!=0u);
  assert((stats.cache_reason_mask&CMP5_CACHE_REASON_EXCLUDED)!=0u);
  assert(stats.cache_like_partial&&stats.partial);
  puts("Excluded content marked as incomplete without touching files: PASS");
  return 0;
 }
 if(argc==3&&strcmp(argv[2],"overflow")==0){
  assert(stats.cache_like_folders==100u);
  assert(stats.cache_ledger_count==CMP5_CACHE_LEDGER_CAP);
  assert(stats.cache_ledger_overflow==1u&&stats.cache_like_partial==1u);
  assert((stats.cache_reason_mask&CMP5_CACHE_REASON_LIMIT)!=0u);
  assert(stats.partial==1u); /* no false complete comparison */
  assert(stats.cache_top_count==CMP5_CACHE_TOP_CAP);
  assert(stats.cache_ledger[0].nominal_bytes==100u);
  assert(strcmp(stats.cache_ledger[0].app_label,"APP099")==0);
  assert(stats.cache_ledger[95].nominal_bytes==5u);
  assert(strcmp(stats.cache_ledger[95].app_label,"APP004")==0);
  for(unsigned i=0;i<CMP5_CACHE_LEDGER_CAP;i++){
   const Cmp5CacheLedger*record=&stats.cache_ledger[i];
   assert(record->key!=0u&&record->metadata_complete==1u);
   assert(record->files==1u&&record->kind==1u);
   assert(record->nominal_bytes==100u-i);
   if(i)assert(stats.cache_ledger[i-1u].nominal_bytes>=record->nominal_bytes);
  }
  puts("96 largest cache metadata records, overflow fail-closed: PASS");
  return 0;
 }
 assert(stats.cache_like_folders==1u&&!stats.cache_like_partial);
 assert(stats.cache_top_count==1u);
 const Cmp5CacheTop*c=&stats.cache_top[0];
 assert(strcmp(c->app_label,"ExampleApp")==0);
 assert(c->kind==1u&&c->nominal_bytes==6u&&c->files==1u);
 assert(c->metadata_complete==1u&&c->metadata_nodes>=2u);
 printf("%" PRIu64 " %" PRIu64 " %u\n",
        c->metadata_xor,c->metadata_sum,c->metadata_nodes);
 return 0;
}
