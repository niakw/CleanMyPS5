#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
/* Host test of the EXACT SHA-256 source-plan encoder linked into the
 * PS5 worker. This test uses synthetic AMPR entries, not console data.
 * GPL-3.0-or-later. */
#include "scan.h"
#include "cmp5_ampr_checkpoint_plan.h"
#include "cmp5_ampr_checkpoint_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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

 /* Real synthetic runtime: a same-size byte change MUST change the v2
  * fingerprint even if path, size, synthetic flag and mtime are unchanged.
  * No game files and no PlayStation hardware are used here. */
 char work[]="/tmp/cmp5-plan-assets-XXXXXX";
 char *root=mkdtemp(work);
 assert(root);
 char folder[512],asset[512],hardlink_path[512];
 assert(snprintf(folder,sizeof(folder),"%s/fakelib",root)>0);
 assert(snprintf(asset,sizeof(asset),"%s/mod.sprx",folder)>0);
 assert(snprintf(hardlink_path,sizeof(hardlink_path),"%s/alias",root)>0);
 assert(mkdir(folder,0700)==0);
 int fd=open(asset,O_WRONLY|O_CREAT|O_EXCL,0600);
 assert(fd>=0);
 assert(write(fd,"ABCD",4)==4);
 assert(close(fd)==0);
 e2a_entry synthetic={0};
 synthetic.rel="fakelib/mod.sprx";
 synthetic.synthetic=true;
 synthetic.action=E2A_LOOSE;
 synthetic.size=4u;
 synthetic.mtime=1700000000;
 files.v=&synthetic;files.n=1u;
 char pinned_bytes[65],changed_bytes[65];
 assert(cmp5_entries_plan_hash_with_assets(&files,root,pinned_bytes)==0);
 fd=open(asset,O_WRONLY|O_NOFOLLOW);
 assert(fd>=0);
 assert(pwrite(fd,"WXYZ",4,0)==4);
 assert(close(fd)==0);
 /* Metadata-only v1 cannot see the replacement, v2 can. */
 assert(cmp5_entries_plan_hash_with_assets(&files,root,changed_bytes)==0);
 assert(strcmp(pinned_bytes,changed_bytes)!=0);
 assert(cmp5_plan_pin_set(checkpoint,pinned_bytes));
 assert(!cmp5_plan_pin_matches(checkpoint,changed_bytes));
 fd=open(asset,O_WRONLY|O_NOFOLLOW);
 assert(fd>=0);
 assert(pwrite(fd,"ABCD",4,0)==4);
 assert(close(fd)==0);
 assert(cmp5_entries_plan_hash_with_assets(&files,root,changed_bytes)==0);
 assert(strcmp(pinned_bytes,changed_bytes)==0);
 synthetic.size=5u;
 assert(cmp5_entries_plan_hash_with_assets(&files,root,changed_bytes)!=0);
 synthetic.size=4u;
 assert(link(asset,hardlink_path)==0);
 assert(cmp5_entries_plan_hash_with_assets(&files,root,changed_bytes)!=0);
 assert(unlink(hardlink_path)==0);
 assert(unlink(asset)==0);
 assert(symlink("/etc/passwd",asset)==0);
 assert(cmp5_entries_plan_hash_with_assets(&files,root,changed_bytes)!=0);
 assert(unlink(asset)==0);
 assert(cmp5_entries_plan_hash_with_assets(&files,root,changed_bytes)!=0);
 assert(rmdir(folder)==0);
 assert(rmdir(root)==0);
 puts("Pinned AMPR file-action SHA v2: byte mutations, aliases and missing plugins fail closed PASS");
 return 0;
}
