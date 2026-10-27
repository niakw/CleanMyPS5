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
 if(argc!=2)return 2;
 Cmp5HomebrewStats stats={0};
 size_t size=0;
 char*json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
 assert(json&&size>100u);
 free(json);
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
