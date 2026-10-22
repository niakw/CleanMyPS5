/* Native media review ABI fixture: never touches PS5 system paths. */
#include "inventory.h"
#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int stop_after_budget(void*opaque){
 unsigned*counter=(unsigned*)opaque;
 return ++(*counter)>5u;
}
int main(int argc,char**argv){
 if(argc!=2&&argc!=3)return 2;
 const int cancel=argc==3&&strcmp(argv[2],"cancel")==0;
 const int longpath=argc==3&&strcmp(argv[2],"longpath")==0;
 const unsigned expected=argc==3&&!cancel&&!longpath?
   (unsigned)strtoul(argv[2],NULL,10):4u;
 size_t json_size=0;
 Cmp5MediaReview review={0};
 unsigned calls=0;
 char*json=cancel?
   cmp5_overview_with_review_cancel(argv[1],&json_size,&review,stop_after_budget,&calls):
   cmp5_overview_with_review(argv[1],&json_size,&review);
 if(cancel){
  assert(json&&json_size>100);
  assert(strstr(json,"\"partial\":true"));
  assert(strstr(json,"\"cleanupValidated\":false"));
  assert(review.partial==1u&&review.count<64u);
  assert(calls>=6u);
  free(json);
  puts("Metadata scan cancellation: partial result, never validated for deletion");
  return 0;
 }
 assert(json&&json_size>100);
 assert(strstr(json,"\"cleanupValidated\":false"));
 if(longpath){
  /* Scan order is unspecified: the long directory may be encountered
   * before some otherwise valid captures. Never assume a full count. */
  assert(review.count<=4u&&review.media_scanned<=4u);
  if(review.partial!=1u||!strstr(json,"\"partial\":true")){
   fprintf(stderr,"Overlong path was wrongly declared complete: count=%u media=%u partial=%u\n",
     review.count,review.media_scanned,review.partial);
   abort();
  }
  free(json);
  puts("Native media review: overlong path reports incomplete coverage");
  return 0;
 }
 assert(review.count==expected);
 assert(review.media_scanned==expected);
 assert(review.partial==0);
 for(unsigned i=1;i<review.count;i++){
  assert(review.entries[i-1].bytes>=review.entries[i].bytes);
 }
 if(expected!=4u){
  for(unsigned i=0;i<review.count;i++){
   assert(review.entries[i].title_id[0]);
   assert(!strchr(review.entries[i].label,'/'));
  }
  free(json);
  puts("Media review: all catalog entries retained, sorted, no filesystem paths");
  return 0;
 }
 const uint64_t mib=1024u*1024u;
 const uint64_t sizes[]={10u*mib,7u*mib,4u*mib,1u*mib};
 for(unsigned i=0;i<review.count;i++){
  const Cmp5MediaCandidate*c=&review.entries[i];
  assert(c->bytes==sizes[i]);
  assert(strcmp(c->title_id,"GAME123")==0);
  assert(strcmp(c->game_name,"Astro's Playroom")==0);
  assert(!strchr(c->label,'/'));
  assert(!strchr(c->label,'\\'));
  for(const unsigned char*p=(const unsigned char*)c->label;*p;p++)
   assert(*p==' '||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9'));
 }
 assert(review.entries[0].media_type==2u);
 assert(review.entries[1].media_type==1u);
 assert(review.entries[0].modified_at==1600000000);
 free(json);
 puts("Media review: ranked, anonymized labels, no hardlink/symlink duplication");
 return 0;
}
