/* Native media review ABI fixture: never touches PS5 system paths. */
#include "inventory.h"
#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char**argv){
 if(argc!=2)return 2;
 size_t json_size=0;
 Cmp5MediaReview review={0};
 char*json=cmp5_overview_with_review(argv[1],&json_size,&review);
 assert(json&&json_size>100);
 assert(strstr(json,"\"cleanupValidated\":false"));
 assert(review.count==4);
 assert(review.media_scanned==4);
 assert(review.partial==0);
 const uint64_t mib=1024u*1024u;
 const uint64_t sizes[]={10u*mib,7u*mib,4u*mib,1u*mib};
 for(unsigned i=0;i<review.count;i++){
  const Cmp5MediaCandidate*c=&review.entries[i];
  assert(c->bytes==sizes[i]);
  assert(strcmp(c->title_id,"GAME123")==0);
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
