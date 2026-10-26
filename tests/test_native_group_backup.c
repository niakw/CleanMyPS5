/* Contract test for exactly the group-writer mode compiled into the
 * PS5 title. Synthetic files in a temporary host root ONLY. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "verified_group.h"
#include "media_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static void snapshot(Cmp5MediaCandidate*out,const char*file){
 struct stat st={0};
 assert(stat(file,&st)==0);
 memset(out,0,sizeof(*out));
 strcpy(out->preview_path,
  "/user/av_contents/photo/NPXS00000/GAME123/REC_001.jpg");
 out->media_type=1u;
 out->bytes=(uint64_t)st.st_size;
 cmp5_snapshot_capture(out,&st);
}
int main(int argc,char**argv){
 if(argc!=4)return 2;
 const char*root=argv[1],*photo=argv[2],*workspace=argv[3];
 Cmp5MediaCandidate primary={0};
 snapshot(&primary,photo);
 Cmp5MediaCandidate parts[CMP5_GROUP_MAX]={{0}};
 uint32_t count=0u;
 assert(cmp5_plan_capture_group(root,&primary,parts,&count,NULL,NULL));
 assert(count==3u&&cmp5_validate_capture_group(parts,count));
 Cmp5GroupReceipt published={0},verified={0};
 assert(cmp5_backup_group(root,parts,count,workspace,"NATIVE_GROUP",
                          &published,NULL,NULL));
 assert(cmp5_verify_group(workspace,"NATIVE_GROUP",&verified,NULL,NULL));
 assert(published.count==3u&&published.count==verified.count);
 assert(published.bytes==verified.bytes);
 assert(memcmp(published.manifest_sha256,verified.manifest_sha256,32u)==0);
 Cmp5OriginMap origins={0};
 assert(cmp5_read_group_origin_map(workspace,"NATIVE_GROUP",&origins,NULL,NULL));
 assert(origins.count==3u);
 for(unsigned i=0;i<3u;i++)
  assert(strcmp(origins.entries[i].path,parts[i].preview_path)==0);
 /* Real read-only original-state inspection linked in the native-mode
  * writer. It must distinguish a missing source from an intact capsule,
  * without any restore or Sony Gallery mutation functions in the title. */
 Cmp5OriginLiveState states[CMP5_GROUP_MAX]={0};
 assert(cmp5_inspect_group_originals(root,workspace,"NATIVE_GROUP",
                                     states,NULL,NULL));
 unsigned photo_position=CMP5_GROUP_MAX;
 for(unsigned i=0;i<3u;i++){
  assert(states[i]==CMP5_ORIGIN_UNCHANGED);
  if(strcmp(origins.entries[i].path,primary.preview_path)==0)
   photo_position=i;
 }
 assert(photo_position<3u);
 char displaced[1024]={0};
 assert(snprintf(displaced,sizeof(displaced),"%s.moved",photo)>0);
 assert(rename(photo,displaced)==0);
 assert(cmp5_inspect_group_originals(root,workspace,"NATIVE_GROUP",
                                     states,NULL,NULL));
 for(unsigned i=0;i<3u;i++)
  assert(states[i]==(i==photo_position?
                      CMP5_ORIGIN_MISSING:CMP5_ORIGIN_UNCHANGED));
 /* Missing originals must NEVER invalidate a genuine independent
  * backup SHA-256 receipt or silently re-create a Gallery path. */
 assert(cmp5_verify_group(workspace,"NATIVE_GROUP",&verified,NULL,NULL));
 assert(rename(displaced,photo)==0);
 assert(cmp5_verify_group(workspace,"NATIVE_GROUP",&verified,NULL,NULL));
 /* O_EXCL: an existing fully verified group must never be overwritten. */
 assert(!cmp5_backup_group(root,parts,count,workspace,"NATIVE_GROUP",
                           &published,NULL,NULL));
 assert(cmp5_verify_group(workspace,"NATIVE_GROUP",NULL,NULL,NULL));
 puts("Native-mode additive group backup / SHA-256 / no clobber: PASS");
 return 0;
}
