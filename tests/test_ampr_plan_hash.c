/* Host test of the EXACT SHA-256 source-plan encoder linked into the
 * PS5 worker. This test uses synthetic AMPR entries, not console data.
 * GPL-3.0-or-later. */
#include "scan.h"
#include "cmp5_ampr_checkpoint_plan.h"
#include "cmp5_ampr_checkpoint_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
 e2a_entry entries[2]={{0}};
 entries[0].rel="eboot.bin";
 entries[0].size=1234567u;
 entries[0].mtime=1700000000;
 entries[0].action=E2A_LOOSE;
 entries[0].layout=E2A_LAYOUT_MIXED;
 entries[1].rel="app0/texture.bin";
 entries[1].size=987654u;
 entries[1].mtime=1700000020;
 entries[1].action=E2A_COMPRESS;
 entries[1].block_shift=16;
 entries[1].layout=E2A_LAYOUT_RANDOM;
 e2a_list files={0};
 files.v=entries;
 files.n=2u;
 char saved[65]={0}, current[65]={0};
 assert(cmp5_entries_plan_hash(&files,saved)==0);
 assert(strlen(saved)==64u);
 assert(cmp5_entries_plan_hash(&files,current)==0);
 assert(strcmp(saved,current)==0);
 uint8_t checkpoint[CMP5_PROFILE_PIN_BYTES]={0};
 assert(cmp5_profile_pin_set(checkpoint,NULL));
 assert(cmp5_plan_pin_set(checkpoint,saved));
 assert(cmp5_plan_pin_matches(checkpoint,saved));

 entries[1].action=E2A_STORE;
 assert(cmp5_entries_plan_hash(&files,current)==0);
 assert(strcmp(saved,current)!=0);
 assert(!cmp5_plan_pin_matches(checkpoint,current));
 entries[1].action=E2A_COMPRESS;
 entries[1].block_shift=17;
 assert(cmp5_entries_plan_hash(&files,current)==0);
 assert(strcmp(saved,current)!=0);
 entries[1].block_shift=16;
 entries[1].layout=E2A_LAYOUT_MIXED;
 assert(cmp5_entries_plan_hash(&files,current)==0);
 assert(strcmp(saved,current)!=0);
 entries[1].layout=E2A_LAYOUT_RANDOM;
 entries[1].hot=true;
 assert(cmp5_entries_plan_hash(&files,current)==0);
 assert(strcmp(saved,current)!=0);
 entries[1].hot=false;
 entries[1].size++;
 assert(cmp5_entries_plan_hash(&files,current)==0);
 assert(strcmp(saved,current)!=0);
 entries[1].size--;
 entries[1].mtime++;
 assert(cmp5_entries_plan_hash(&files,current)==0);
 assert(strcmp(saved,current)!=0);
 entries[1].mtime--;
 entries[1].rel="app0/renamed.bin";
 assert(cmp5_entries_plan_hash(&files,current)==0);
 assert(strcmp(saved,current)!=0);
 entries[1].rel="app0/texture.bin";
 files.n=1u;
 assert(cmp5_entries_plan_hash(&files,current)==0);
 assert(strcmp(saved,current)!=0);
 files.n=2u;
 e2a_entry reordered[2]={entries[1],entries[0]};
 files.v=reordered;
 assert(cmp5_entries_plan_hash(&files,current)==0);
 assert(strcmp(saved,current)!=0);
 files.v=entries;

 /* Synthetic fakelib data are regenerated during each resume.
  * Its mtime is volatile, but its path/size/action are still pinned. */
 entries[1].synthetic=true;
 assert(cmp5_entries_plan_hash(&files,saved)==0);
 entries[1].mtime+=600;
 assert(cmp5_entries_plan_hash(&files,current)==0);
 assert(strcmp(saved,current)==0);
 entries[1].size++;
 assert(cmp5_entries_plan_hash(&files,current)==0);
 assert(strcmp(saved,current)!=0);
 entries[1].size--;
 /* The origin/synthetic classification is itself part of the plan. */
 entries[1].synthetic=false;
 entries[1].mtime-=600;
 assert(cmp5_entries_plan_hash(&files,current)==0);
 assert(strcmp(saved,current)!=0);
 entries[1].synthetic=true;
 assert(cmp5_entries_plan_hash(&files,current)==0);
 assert(strcmp(saved,current)==0);

 entries[1].rel=NULL;
 assert(cmp5_entries_plan_hash(&files,current)!=0);
 files.v=NULL;
 assert(cmp5_entries_plan_hash(&files,current)!=0);
 files.v=entries;files.n=0;
 assert(cmp5_entries_plan_hash(&files,current)==0);
 puts("Pinned AMPR file-action SHA: changed size/date/action/layout/name detected; synthetic mtime stable PASS");
 return 0;
}
