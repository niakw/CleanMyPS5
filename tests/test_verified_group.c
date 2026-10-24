#define _DEFAULT_SOURCE
#include "verified_group.h"
#include "media_snapshot.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int stop(void*context){(void)context;return 1;}
static int stop_during_copy(void*context){
 unsigned *calls=(unsigned*)context;
 return ++(*calls)>10u;
}
/* Called once before opening the selected gallery file, and again after
 * its initial fstat but before reading directory entries. Rewrite in place
 * on the second callback to exercise the metadata TOCTOU guard. */
typedef struct {const char*path;unsigned calls;int rewritten;} PrimaryMutation;
static int rewrite_primary_during_scan(void*context){
 PrimaryMutation*state=(PrimaryMutation*)context;
 if(++state->calls==1u){
  usleep(10000); /* Different nanosecond timestamps on host filesystems. */
 }else if(state->calls==2u){
  FILE*f=fopen(state->path,"r+b");
  if(!f)return 1;
  const int written=fputc('Z',f);
  state->rewritten=(written=='Z'&&fclose(f)==0);
  if(!state->rewritten)return 1;
 }
 return 0;
}
typedef struct {char late[512];unsigned calls;int created;} ScanMutation;
static int add_unrelated_during_scan(void*context){
 ScanMutation*state=(ScanMutation*)context;
 if(++state->calls==2u){
  FILE*f=fopen(state->late,"wb");
  if(!f)return 1;
  state->created=fputs("DELIBERATE_DIR_MUTATION",f)>=0&&fclose(f)==0;
  if(!state->created)return 1;
 }
 return 0;
}
typedef struct {
 char first_published[512];
 char late_variant[512];
 int created;
} LateCompanion;
static int add_companion_during_backup(void*context){
 LateCompanion*state=(LateCompanion*)context;
 if(!state->created&&access(state->first_published,F_OK)==0){
  FILE*file=fopen(state->late_variant,"wb");
  if(!file)return 1;
  state->created=fputs("LATE_CAPTURE_VARIANT",file)>=0&&fclose(file)==0;
  if(!state->created)return 1;
 }
 return 0;
}
static void snapshot(Cmp5MediaCandidate*c,const char*absolute,
 const char*relative,unsigned type){
 struct stat st;assert(stat(absolute,&st)==0);
 memset(c,0,sizeof(*c));c->media_type=type;
 c->bytes=(uint64_t)st.st_size;
 cmp5_snapshot_capture(c,&st);
 assert(strlen(relative)<sizeof(c->preview_path));
 strcpy(c->preview_path,relative);
}
int main(int argc,char**argv){
 if(argc!=9)return 2;
 const char*root=argv[1],*jxr=argv[2],*jpg=argv[3],*dat=argv[4];
 const char*private=argv[5],*restored=argv[6];
 const char*big_video=argv[7],*big_sidecar=argv[8];
 const char*folder="/user/av_contents/photo/NPXS00000/GAME123/REC_001";
 Cmp5MediaCandidate media[3];
 char relative[256];
 const char*paths[3]={jxr,jpg,dat};
 const char*exts[3]={".jxr",".jpg",".dat"};
 for(unsigned i=0;i<3;i++){
  (void)snprintf(relative,sizeof(relative),"%s%s",folder,exts[i]);
  snapshot(&media[i],paths[i],relative,i==2?3:1);
 }
 /* Snapshot metadata may be supplied by a corrupt or untrusted caller.
  * Validate fixed buffers before any unbounded C string traversal, then
  * enforce media-family and suffix consistency for host backup groups. */
 Cmp5MediaCandidate malformed[3];
 assert(cmp5_validate_capture_group(media,3));
 memcpy(malformed,media,sizeof(malformed));
 memset(malformed[0].preview_path,'A',sizeof(malformed[0].preview_path));
 assert(!cmp5_validate_capture_group(malformed,3));
 memcpy(malformed,media,sizeof(malformed));
 memset(malformed[1].preview_path,'B',sizeof(malformed[1].preview_path));
 assert(!cmp5_validate_capture_group(malformed,3));
 memcpy(malformed,media,sizeof(malformed));
 strcpy(malformed[0].preview_path,
   "/user/av_contents/photo/NPXS00000/GAME123/REC_001.mp4");
 assert(!cmp5_validate_capture_group(malformed,3));
 memcpy(malformed,media,sizeof(malformed));
 malformed[1].media_type=2u;
 assert(!cmp5_validate_capture_group(malformed,3));
 memcpy(malformed,media,sizeof(malformed));
 strcpy(malformed[1].preview_path,
   "/user/av_contents/video/NPXS00000/GAME123/REC_001.jpg");
 assert(!cmp5_validate_capture_group(malformed,3));
 memcpy(malformed,media,sizeof(malformed));
 strcpy(malformed[1].preview_path,
   "/user/av_contents/photo/NPXS00000/../GAME123/REC_001.jpg");
 assert(!cmp5_validate_capture_group(malformed,3));
 memcpy(malformed,media,sizeof(malformed));
 strcpy(malformed[1].preview_path,
   "/user/av_contents/photo/NPXS00000/GAME123/REC_001.gif");
 assert(!cmp5_validate_capture_group(malformed,3));
 memcpy(malformed,media,sizeof(malformed));
 malformed[2].bytes=65537u;
 assert(!cmp5_validate_capture_group(malformed,3));
 memcpy(malformed,media,sizeof(malformed));
 strcpy(malformed[1].preview_path,
   "/user/av_contents/photo/NPXS00000/GAME123/REC_001.JPG");
 assert(cmp5_validate_capture_group(malformed,3));
 /* Large recordings are safe to INSPECT without reading their contents,
  * but remain outside the capped host backup contract. */
 Cmp5MediaCandidate large[2];
 snapshot(&large[0],big_video,
   "/user/av_contents/video/NPXS00000/GAME123/BIG_001.mp4",2u);
 snapshot(&large[1],big_sidecar,
   "/user/av_contents/video/NPXS00000/GAME123/BIG_001.dat",3u);
 assert(large[0].bytes>512ULL*1024ULL*1024ULL);
 assert(!cmp5_validate_capture_group(large,2));
 Cmp5CompanionReport large_report={0};
 assert(cmp5_inspect_capture_companions(root,&large[0],&large_report,NULL,NULL));
 assert(large_report.state==CMP5_COMPANIONS_MULTIPLE&&
        large_report.files==2u&&large_report.media==1u&&
        large_report.sidecars==1u&&
        large_report.nominal_bytes==large[0].bytes+large[1].bytes);
 Cmp5MediaCandidate too_big_group[CMP5_GROUP_MAX]={{0}};
 uint32_t too_big_count=42u;
 assert(!cmp5_plan_capture_group(root,&large[0],too_big_group,
         &too_big_count,NULL,NULL));
 assert(too_big_count==0u);
 for(unsigned i=0;i<CMP5_GROUP_MAX;i++)
  assert(!too_big_group[i].bytes&&!too_big_group[i].preview_path[0]);
 Cmp5GroupReceipt oversized_receipt={0};
 assert(!cmp5_backup_group(root,large,2,private,"BIGBACKUP",
                           &oversized_receipt,NULL,NULL));
 assert(!oversized_receipt.count&&!oversized_receipt.bytes);
 /* A very busy video directory must report a specific bounded-scan limit,
  * not silently claim there are no companion files or that access is denied. */
 char noise[512];
 for(unsigned i=0;i<1030u;i++){
  const int n=snprintf(noise,sizeof(noise),"%.*s/UNRELATED_%04u.tmp",
    (int)(strrchr(big_video,'/')-big_video),big_video,i);
  assert(n>0&&(size_t)n<sizeof(noise));
  FILE*item=fopen(noise,"wb");assert(item);
  assert(fputs("x",item)>=0&&fclose(item)==0);
 }
 Cmp5CompanionReport crowded={0};
 assert(!cmp5_inspect_capture_companions(root,&large[0],&crowded,NULL,NULL));
 assert(crowded.state==CMP5_COMPANIONS_UNVERIFIABLE&&crowded.files==0u);
 assert(crowded.nominal_bytes==0u&&crowded.failure==
   CMP5_COMPANION_FAILURE_DIRECTORY_LIMIT);
 for(unsigned i=0;i<1030u;i++){
  (void)snprintf(noise,sizeof(noise),"%.*s/UNRELATED_%04u.tmp",
    (int)(strrchr(big_video,'/')-big_video),big_video,i);
  assert(unlink(noise)==0);
 }
 assert(cmp5_inspect_capture_companions(root,&large[0],&crowded,NULL,NULL));
 assert(crowded.state==CMP5_COMPANIONS_MULTIPLE&&
        crowded.failure==CMP5_COMPANION_FAILURE_NONE);
 Cmp5GroupReceipt a={0},b={0};
 Cmp5MediaCandidate found[CMP5_GROUP_MAX];
 uint32_t found_count=0;
 assert(!cmp5_plan_capture_group(root,&media[0],found,&found_count,stop,NULL));
 assert(found_count==0);
 assert(cmp5_plan_capture_group(root,&media[0],found,&found_count,NULL,NULL));
 assert(found_count==3u);
 assert(found[0].media_type==3u&&found[1].media_type==1u&&
        found[2].media_type==1u);
 assert(found[0].bytes==media[2].bytes&&found[1].bytes==media[1].bytes&&
        found[2].bytes==media[0].bytes);
 /* Read-only UI inspection must distinguish one stable visible media file
  * from permission or integrity failures, while backup planner still
  * refuses to treat that singleton as a restorable group. */
 Cmp5CompanionReport observed={0};
 assert(cmp5_inspect_capture_companions(root,&media[0],&observed,NULL,NULL));
 assert(observed.state==CMP5_COMPANIONS_MULTIPLE);
 assert(observed.files==3u&&observed.media==2u&&observed.sidecars==1u);
 assert(observed.nominal_bytes==media[0].bytes+media[1].bytes+media[2].bytes);
 assert(!cmp5_inspect_capture_companions(root,&media[0],&observed,stop,NULL));
 assert(observed.state==CMP5_COMPANIONS_UNVERIFIABLE&&observed.files==0u);
 assert(observed.failure==CMP5_COMPANION_FAILURE_CANCELLED);
 char unavailable_root[512];
 const int root_len=snprintf(unavailable_root,sizeof(unavailable_root),
   "%s/NO_SUCH_SOURCE_FOLDER",root);
 assert(root_len>0&&(size_t)root_len<sizeof(unavailable_root));
 assert(!cmp5_inspect_capture_companions(unavailable_root,&media[0],
         &observed,NULL,NULL));
 assert(observed.failure==CMP5_COMPANION_FAILURE_UNAVAILABLE&&
        observed.state==CMP5_COMPANIONS_UNVERIFIABLE&&observed.files==0u);
 const char*slash=strrchr(jxr,'/');assert(slash);
 char single_path[512];
 const int single_len=snprintf(single_path,sizeof(single_path),"%.*s/SOLO.jpg",
   (int)(slash-jxr),jxr);
 assert(single_len>0&&(size_t)single_len<sizeof(single_path));
 FILE*single=fopen(single_path,"wb");assert(single);
 assert(fputs("ONLY_MEDIA_NO_VISIBLE_COMPANION",single)>=0&&fclose(single)==0);
 Cmp5MediaCandidate lone;
 snapshot(&lone,single_path,
   "/user/av_contents/photo/NPXS00000/GAME123/SOLO.jpg",1u);
 assert(!cmp5_plan_capture_group(root,&lone,found,&found_count,NULL,NULL));
 assert(found_count==0u);
 assert(cmp5_inspect_capture_companions(root,&lone,&observed,NULL,NULL));
 assert(observed.state==CMP5_COMPANIONS_SINGLE);
 assert(observed.files==1u&&observed.media==1u&&observed.sidecars==0u);
 assert(observed.nominal_bytes==lone.bytes);
 /* Hidden same-stem companion files must not be silently skipped, even
  * though unrelated dotfiles remain ignored. */
 char hidden_sidecar[512];
 const int hidden_length=snprintf(hidden_sidecar,sizeof(hidden_sidecar),
   "%.*s/.SOLO.dat",(int)(slash-jxr),jxr);
 assert(hidden_length>0&&(size_t)hidden_length<sizeof(hidden_sidecar));
 FILE*hidden=fopen(hidden_sidecar,"wb");assert(hidden);
 assert(fputs("INCOMPLETE_SIDECAR",hidden)>=0&&fclose(hidden)==0);
 assert(!cmp5_inspect_capture_companions(root,&lone,&observed,NULL,NULL));
 assert(observed.state==CMP5_COMPANIONS_UNVERIFIABLE&&
        observed.failure==CMP5_COMPANION_FAILURE_AMBIGUOUS);
 assert(unlink(hidden_sidecar)==0);
 assert(cmp5_inspect_capture_companions(root,&lone,&observed,NULL,NULL));
 assert(observed.state==CMP5_COMPANIONS_SINGLE);
 /* Third-party PS5 gallery implementations have observed .meta and .ext
  * sidecars. They remain outside this prototype's restore contract; show
  * a distinct unsupported-dependency status, never a false complete set. */
 for(unsigned k=0;k<2u;k++){
  const char*extension=k==0u?".meta":".ext";
  char unsupported[512];
  const int length=snprintf(unsupported,sizeof(unsupported),
    "%.*s/SOLO%s",(int)(slash-jxr),jxr,extension);
  assert(length>0&&(size_t)length<sizeof(unsupported));
  FILE*sidecar=fopen(unsupported,"wb");assert(sidecar);
  assert(fputs("REQUIRES_GALLERY_SUPPORT",sidecar)>=0&&fclose(sidecar)==0);
  assert(!cmp5_inspect_capture_companions(root,&lone,&observed,NULL,NULL));
  assert(observed.state==CMP5_COMPANIONS_UNVERIFIABLE&&
    observed.failure==CMP5_COMPANION_FAILURE_UNSUPPORTED_DEPENDENCY);
  assert(unlink(unsupported)==0);
 }
 assert(cmp5_inspect_capture_companions(root,&lone,&observed,NULL,NULL));
 assert(observed.state==CMP5_COMPANIONS_SINGLE);
 /* In-place rewrite after initial fstat, before enumeration: reject a
  * same-inode/same-size primary whose nanosecond ctime/mtime changes. */
 PrimaryMutation rewritten={single_path,0u,0};
 assert(!cmp5_inspect_capture_companions(root,&lone,&observed,
        rewrite_primary_during_scan,&rewritten));
 assert(rewritten.rewritten&&rewritten.calls>=2u);
 assert(observed.state==CMP5_COMPANIONS_UNVERIFIABLE&&
        observed.failure==CMP5_COMPANION_FAILURE_CHANGED);
 snapshot(&lone,single_path,
   "/user/av_contents/photo/NPXS00000/GAME123/SOLO.jpg",1u);
 assert(cmp5_inspect_capture_companions(root,&lone,&observed,NULL,NULL));
 assert(observed.state==CMP5_COMPANIONS_SINGLE);
 Cmp5MediaCandidate invalid_lone=lone;
 invalid_lone.snapshot_inode++;
 assert(!cmp5_inspect_capture_companions(root,&invalid_lone,&observed,NULL,NULL));
 assert(observed.state==CMP5_COMPANIONS_UNVERIFIABLE&&observed.files==0u);
 assert(observed.failure==CMP5_COMPANION_FAILURE_CHANGED);
 char suspicious_path[512];
 const int suspicious_len=snprintf(suspicious_path,sizeof(suspicious_path),
   "%.*s/SOLO.png",(int)(slash-jxr),jxr);
 assert(suspicious_len>0&&(size_t)suspicious_len<sizeof(suspicious_path));
 assert(symlink(jxr,suspicious_path)==0);
 assert(!cmp5_inspect_capture_companions(root,&lone,&observed,NULL,NULL));
 assert(observed.state==CMP5_COMPANIONS_UNVERIFIABLE);
 assert(observed.failure==CMP5_COMPANION_FAILURE_AMBIGUOUS);
 assert(unlink(suspicious_path)==0);
 assert(unlink(single_path)==0);

 /* Reject stale subsecond primary identity before creating any capsule. */
 Cmp5MediaCandidate nanos[3];memcpy(nanos,media,sizeof(nanos));
 nanos[0].snapshot_mtime_nsec=(nanos[0].snapshot_mtime_nsec+1u)%1000000000u;
 assert(!cmp5_backup_group(root,nanos,3,private,"NANO_STALE",&a,NULL,NULL));
 char nanofile[512];
 (void)snprintf(nanofile,sizeof(nanofile),"%s/NANO_STALE__1",private);
 assert(access(nanofile,F_OK)!=0);
 /* The backup API, not only its pure validator, must reject an
  * unterminated gallery locator before opening/creating any private files. */
 memcpy(malformed,media,sizeof(malformed));
 memset(malformed[0].preview_path,'X',sizeof(malformed[0].preview_path));
 assert(!cmp5_backup_group(root,malformed,3,private,"BADPATH",&a,NULL,NULL));
 char no_stage[512],no_commit[512];
 (void)snprintf(no_stage,sizeof(no_stage),"%s/BADPATH__1",private);
 (void)snprintf(no_commit,sizeof(no_commit),"%s/BADPATH__COMMIT",private);
 assert(access(no_stage,F_OK)!=0&&access(no_commit,F_OK)!=0);
 /* A structurally valid 2-item subset is NOT a complete observable
  * capture group while REC_001.dat is present. Reject before any staging. */
 assert(cmp5_validate_capture_group(media,2));
 assert(!cmp5_backup_group(root,media,2,private,"OMITTED",&a,NULL,NULL));
 char omitted[512];
 (void)snprintf(omitted,sizeof(omitted),"%s/OMITTED__1",private);
 assert(access(omitted,F_OK)!=0);
 (void)snprintf(omitted,sizeof(omitted),"%s/OMITTED__COMMIT",private);
 assert(access(omitted,F_OK)!=0);
 Cmp5MediaCandidate missing_jpg[2]={media[0],media[2]};
 assert(cmp5_validate_capture_group(missing_jpg,2));
 assert(!cmp5_backup_group(root,missing_jpg,2,private,"OMITTEDDAT",&a,NULL,NULL));
 (void)snprintf(omitted,sizeof(omitted),"%s/OMITTEDDAT__1",private);
 assert(access(omitted,F_OK)!=0);
 assert(!cmp5_backup_group(root,media,3,private,"ALBUM",&a,stop,NULL));
 assert(!cmp5_verify_group(private,"ALBUM",&a,NULL,NULL));
 unsigned calls=0;
 assert(!cmp5_backup_group(root,media,3,private,"ABORTED",&a,
  stop_during_copy,&calls));
 assert(calls>10u);
 assert(!cmp5_verify_group(private,"ABORTED",&a,NULL,NULL));
 /* Simulate a new same-stem image appearing after the first capsule has
  * been published but before COMMIT. Even when all original bytes verify,
  * an unaccounted companion must prevent declaring a complete group. */
 LateCompanion late={0};
 (void)snprintf(late.first_published,sizeof(late.first_published),
   "%s/RACEADD__1",private);
 const int late_name_length=snprintf(late.late_variant,sizeof(late.late_variant),
   "%.*s/REC_001.png",(int)(strrchr(jxr,'/')-jxr),jxr);
 assert(late_name_length>0&&(size_t)late_name_length<sizeof(late.late_variant));
 assert(!cmp5_backup_group(root,media,3,private,"RACEADD",&a,
   add_companion_during_backup,&late));
 assert(late.created);
 char late_commit[512];
 (void)snprintf(late_commit,sizeof(late_commit),"%s/RACEADD__COMMIT",private);
 assert(access(late_commit,F_OK)!=0);
 Cmp5RecoveryReport race_audit={0};
 assert(cmp5_inspect_group_recovery(private,"RACEADD",&race_audit));
 assert(race_audit.state==CMP5_RECOVERY_INCOMPLETE&&
        race_audit.published_parts>=1u&&!race_audit.marker_present);
 assert(unlink(late.late_variant)==0);
 /* Stale private capsule staging for a later member must prevent even
  * the FIRST write of the group (the old code failed only on member 3). */
 char stale_backup[512],early_backup_member[512];
 (void)snprintf(stale_backup,sizeof(stale_backup),"%s/.ALBUM__3.part",private);
 (void)snprintf(early_backup_member,sizeof(early_backup_member),
   "%s/ALBUM__1",private);
 FILE*stale=fopen(stale_backup,"wb");assert(stale);
 assert(fputs("OLDER_INTERRUPTED_BACKUP",stale)>=0&&fclose(stale)==0);
 assert(!cmp5_backup_group(root,media,3,private,"ALBUM",&a,NULL,NULL));
 assert(access(early_backup_member,F_OK)!=0);
 assert(unlink(stale_backup)==0);
 /* Original captures remain unchanged; a clean fresh ID can now commit. */
 assert(cmp5_backup_group(root,media,3,private,"ALBUM",&a,NULL,NULL));
 assert(a.count==3&&a.bytes==media[0].bytes+media[1].bytes+media[2].bytes);
 assert(cmp5_verify_group(private,"ALBUM",&b,NULL,NULL));
 assert(!memcmp(a.manifest_sha256,b.manifest_sha256,32));
 /* A complete group must be verifiable, while missing IDs remain empty. */
 Cmp5RecoveryReport recovery={0};
 assert(cmp5_inspect_group_recovery(private,"ALBUM",&recovery));
 assert(recovery.state==CMP5_RECOVERY_VERIFIED);
 assert(recovery.published_parts==3u&&recovery.staged_parts==0u);
 assert(recovery.marker_present);
 assert(recovery.unexpected_entries==0u);
 /* An unexpected fifth capsule and its staged form cannot be silently
  * ignored by the read-only audit of a four-member namespace. */
 char extra_unknown[512];
 (void)snprintf(extra_unknown,sizeof(extra_unknown),"%s/ALBUM__5",private);
 FILE*unexpected=fopen(extra_unknown,"wb");assert(unexpected);
 assert(fputs("SURPLUS",unexpected)>=0&&fclose(unexpected)==0);
 assert(cmp5_inspect_group_recovery(private,"ALBUM",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INVALID&&
        recovery.unexpected_entries==1u);
 assert(unlink(extra_unknown)==0);
 (void)snprintf(extra_unknown,sizeof(extra_unknown),"%s/.ALBUM__5.part",private);
 unexpected=fopen(extra_unknown,"wb");assert(unexpected);
 assert(fputs("STAGE",unexpected)>=0&&fclose(unexpected)==0);
 assert(cmp5_inspect_group_recovery(private,"ALBUM",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INVALID&&
        recovery.unexpected_entries==1u);
 assert(unlink(extra_unknown)==0);
 assert(cmp5_inspect_group_recovery(private,"ALBUM",&recovery));
 assert(recovery.state==CMP5_RECOVERY_VERIFIED&&
        recovery.unexpected_entries==0u);
 /* A committed marker must not hide interrupted stages or extra capsules. */
 char extra_stage[512];
 (void)snprintf(extra_stage,sizeof(extra_stage),"%s/.ALBUM__2.part",private);
 FILE*stage=fopen(extra_stage,"wb");assert(stage);
 assert(fputs("stale stage",stage)>=0&&fclose(stage)==0);
 assert(cmp5_inspect_group_recovery(private,"ALBUM",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INVALID);
 assert(recovery.staged_parts==1u&&recovery.marker_present);
 assert(unlink(extra_stage)==0);
 Cmp5BackupReceipt extra={0};
 assert(cmp5_backup_media(root,&media[0],private,"ALBUM__4",&extra,NULL,NULL));
 assert(cmp5_inspect_group_recovery(private,"ALBUM",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INVALID&&recovery.published_parts==4u);
 char surplus[512];
 (void)snprintf(surplus,sizeof(surplus),"%s/ALBUM__4",private);
 assert(unlink(surplus)==0);
 assert(cmp5_inspect_group_recovery(private,"ALBUM",&recovery));
 assert(recovery.state==CMP5_RECOVERY_VERIFIED);
 assert(cmp5_inspect_group_recovery(private,"NEVER_BACKED_UP",&recovery));
 assert(recovery.state==CMP5_RECOVERY_EMPTY);
 assert(!recovery.published_parts&&!recovery.staged_parts&&!recovery.marker_present);
 /* Simulate an interrupted transaction with only one committed capsule. */
 Cmp5BackupReceipt orphan={0};
 assert(cmp5_backup_media(root,&media[0],private,"RECOV__1",&orphan,NULL,NULL));
 assert(cmp5_inspect_group_recovery(private,"RECOV",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INCOMPLETE);
 assert(recovery.published_parts==1u&&!recovery.marker_present);
 char planted[512];
 (void)snprintf(planted,sizeof(planted),"%s/.RECOV__2.part",private);
 FILE*partial=fopen(planted,"wb");assert(partial);
 assert(fputs("INCOMPLETE",partial)>=0&&fclose(partial)==0);
 assert(cmp5_inspect_group_recovery(private,"RECOV",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INCOMPLETE&&recovery.staged_parts==1u);
 assert(unlink(planted)==0);
 assert(symlink(jxr,planted)==0);
 assert(cmp5_inspect_group_recovery(private,"RECOV",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INVALID);
 assert(unlink(planted)==0);
 assert(cmp5_inspect_group_recovery(private,"RECOV",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INCOMPLETE);
 /* A forged commit marker must never make a partial group appear valid. */
 (void)snprintf(planted,sizeof(planted),"%s/RECOV__COMMIT",private);
 assert(symlink(jxr,planted)==0);
 assert(cmp5_inspect_group_recovery(private,"RECOV",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INVALID&&recovery.marker_present);
 assert(unlink(planted)==0);
 /* Likewise reject hardlinked capsules even in a private workspace. */
 char first_member[512],second_member[512];
 (void)snprintf(first_member,sizeof(first_member),"%s/RECOV__1",private);
 (void)snprintf(second_member,sizeof(second_member),"%s/RECOV__2",private);
 assert(link(first_member,second_member)==0);
 assert(cmp5_inspect_group_recovery(private,"RECOV",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INVALID);
 assert(unlink(second_member)==0);
 assert(cmp5_inspect_group_recovery(private,"RECOV",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INCOMPLETE);
 assert(!cmp5_inspect_group_recovery(private,"BAD/ID",&recovery));

 assert(!cmp5_backup_group(root,media,3,private,"ALBUM",&b,NULL,NULL));
 Cmp5RecoveryPairReport pair={0};
 assert(!cmp5_inspect_backup_restore_pair(private,"ALBUM",private,
   "ALBUM",&pair)); /* Same actual directory cannot be a restore. */
 char alias_workspace[512];
 const int alias_length=snprintf(alias_workspace,sizeof(alias_workspace),"%s/.",private);
 assert(alias_length>0&&(size_t)alias_length<sizeof(alias_workspace));
 assert(!cmp5_inspect_backup_restore_pair(private,"ALBUM",alias_workspace,
   "ALBUM",&pair)); /* Different spelling, identical directory inode. */
 assert(!cmp5_inspect_backup_restore_pair(private,"BAD/ID",restored,
   "ALBUM",&pair));
 assert(cmp5_inspect_backup_restore_pair(private,"ALBUM",restored,
   "ALBUM",&pair));
 assert(pair.state==CMP5_PAIR_NOT_READY);
 assert(pair.backup.state==CMP5_RECOVERY_VERIFIED&&
        pair.restored.state==CMP5_RECOVERY_EMPTY);
 /* A backup that still passes its individual COMMIT digest but has
  * a stray .part or unknown capsule is NOT a safe restore source.
  * The destination must remain entirely untouched on rejection. */
 char ambiguous[512];
 (void)snprintf(ambiguous,sizeof(ambiguous),"%s/.ALBUM__4.part",private);
 FILE*ambiguous_part=fopen(ambiguous,"wb");assert(ambiguous_part);
 assert(fputs("INTERRUPTED_SOURCE",ambiguous_part)>=0&&
        fclose(ambiguous_part)==0);
 char destination_first[512];
 (void)snprintf(destination_first,sizeof(destination_first),
   "%s/ALBUM__1",restored);
 assert(!cmp5_restore_group(private,"ALBUM",restored,&b,NULL,NULL));
 assert(access(destination_first,F_OK)!=0);
 assert(unlink(ambiguous)==0);
 (void)snprintf(ambiguous,sizeof(ambiguous),"%s/ALBUM__5",private);
 ambiguous_part=fopen(ambiguous,"wb");assert(ambiguous_part);
 assert(fputs("UNEXPECTED_CAPSULE",ambiguous_part)>=0&&
        fclose(ambiguous_part)==0);
 assert(!cmp5_restore_group(private,"ALBUM",restored,&b,NULL,NULL));
 assert(access(destination_first,F_OK)!=0);
 assert(unlink(ambiguous)==0);
 /* A leftover later member must block the entire restore before the
  * first restored file appears, without touching the older partial. */
 char stale_restore[512],early_restored_member[512];
 (void)snprintf(stale_restore,sizeof(stale_restore),
   "%s/.ALBUM__3.part",restored);
 (void)snprintf(early_restored_member,sizeof(early_restored_member),
   "%s/ALBUM__1",restored);
 FILE*stale_restore_file=fopen(stale_restore,"wb");assert(stale_restore_file);
 assert(fputs("OLDER_INTERRUPTED_RESTORE",stale_restore_file)>=0&&
        fclose(stale_restore_file)==0);
 assert(!cmp5_restore_group(private,"ALBUM",restored,&b,NULL,NULL));
 assert(access(early_restored_member,F_OK)!=0);
 assert(unlink(stale_restore)==0);
 assert(!cmp5_restore_group(private,"ALBUM",restored,&b,stop,NULL));
 assert(!cmp5_verify_restored_group(restored,"ALBUM",&b,NULL,NULL));
 assert(cmp5_restore_group(private,"ALBUM",restored,&b,NULL,NULL));
 assert(cmp5_verify_restored_group(restored,"ALBUM",&b,NULL,NULL));
 assert(cmp5_inspect_restored_group_recovery(restored,"ALBUM",&recovery));
 assert(recovery.state==CMP5_RECOVERY_VERIFIED&&
        recovery.published_parts==3u&&recovery.marker_present&&
        recovery.unexpected_entries==0u);
 assert(cmp5_inspect_backup_restore_pair(private,"ALBUM",restored,
   "ALBUM",&pair));
 assert(pair.state==CMP5_PAIR_MATCH);
 assert(pair.backup.state==CMP5_RECOVERY_VERIFIED&&
        pair.restored.state==CMP5_RECOVERY_VERIFIED);
 /* A second, independently valid two-member album must NOT correlate
  * to the 3-member restored contents of the first album. */
 Cmp5GroupReceipt shorter={0};
 /* An independent, complete 2-file fixture exercises content mismatch.
  * Using a subset of REC_001 here would incorrectly bypass the new guard. */
 Cmp5MediaCandidate alternate[2]={{0}};
 char alternate_absolute[512],alternate_relative[256];
 const char*alt_ext[2]={".jpg",".dat"};
 for(unsigned k=0;k<2;k++){
  const int n=snprintf(alternate_absolute,sizeof(alternate_absolute),
    "%.*s/ALT_002%s",(int)(strrchr(jxr,'/')-jxr),jxr,alt_ext[k]);
  assert(n>0&&(size_t)n<sizeof(alternate_absolute));
  FILE*alt=fopen(alternate_absolute,"wb");assert(alt);
  assert(fputs(k==0?"OTHER_CAPTURE_IMAGE":"OTHER_METADATA",alt)>=0);
  assert(fclose(alt)==0);
  const int relative_length=snprintf(alternate_relative,sizeof(alternate_relative),
    "/user/av_contents/photo/NPXS00000/GAME123/ALT_002%s",alt_ext[k]);
  assert(relative_length>0&&(size_t)relative_length<sizeof(alternate_relative));
  snapshot(&alternate[k],alternate_absolute,alternate_relative,k==0?1u:3u);
 }
 assert(cmp5_backup_group(root,alternate,2,private,"SHORT",&shorter,NULL,NULL));
 assert(cmp5_restore_group(private,"SHORT",restored,&shorter,NULL,NULL));
 assert(cmp5_inspect_backup_restore_pair(private,"ALBUM",restored,
   "SHORT",&pair));
 assert(pair.state==CMP5_PAIR_MISMATCH);
 assert(pair.backup.state==CMP5_RECOVERY_VERIFIED&&
        pair.restored.state==CMP5_RECOVERY_VERIFIED);
 assert(cmp5_inspect_backup_restore_pair(private,"ALBUM",restored,
   "ALBUM",&pair)&&pair.state==CMP5_PAIR_MATCH);
 assert(cmp5_inspect_restored_group_recovery(restored,"NEVER_RESTORED",&recovery));
 assert(recovery.state==CMP5_RECOVERY_EMPTY);
 char restore_surplus[512];
 (void)snprintf(restore_surplus,sizeof(restore_surplus),"%s/ALBUM__5",restored);
 unexpected=fopen(restore_surplus,"wb");assert(unexpected);
 assert(fputs("UNKNOWN",unexpected)>=0&&fclose(unexpected)==0);
 assert(cmp5_inspect_restored_group_recovery(restored,"ALBUM",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INVALID&&
        recovery.unexpected_entries==1u);
 assert(cmp5_inspect_backup_restore_pair(private,"ALBUM",restored,
   "ALBUM",&pair)&&pair.state==CMP5_PAIR_INVALID);
 assert(unlink(restore_surplus)==0);
 (void)snprintf(restore_surplus,sizeof(restore_surplus),"%s/.ALBUM__1.part",restored);
 unexpected=fopen(restore_surplus,"wb");assert(unexpected);
 assert(fputs("INTERRUPTED",unexpected)>=0&&fclose(unexpected)==0);
 assert(cmp5_inspect_restored_group_recovery(restored,"ALBUM",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INVALID&&recovery.staged_parts==1u);
 assert(unlink(restore_surplus)==0);
 (void)snprintf(restore_surplus,sizeof(restore_surplus),"%s/WAITING__1",restored);
 unexpected=fopen(restore_surplus,"wb");assert(unexpected);
 assert(fputs("UNCOMMITTED",unexpected)>=0&&fclose(unexpected)==0);
 assert(cmp5_inspect_restored_group_recovery(restored,"WAITING",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INCOMPLETE&&
        recovery.published_parts==1u&&!recovery.marker_present);
 assert(unlink(restore_surplus)==0);
 assert(cmp5_inspect_restored_group_recovery(restored,"ALBUM",&recovery));
 assert(recovery.state==CMP5_RECOVERY_VERIFIED);
 assert(!cmp5_inspect_restored_group_recovery(restored,"BAD/ID",&recovery));
 assert(b.count==3);
 assert(!cmp5_restore_group(private,"ALBUM",restored,&b,NULL,NULL));
 Cmp5MediaCandidate wrong[3];memcpy(wrong,media,sizeof(wrong));
 strcpy(wrong[1].preview_path,"/user/av_contents/photo/NPXS00000/GAME123/OTHER.jpg");
 assert(!cmp5_backup_group(root,wrong,3,private,"MIXED",&b,NULL,NULL));
 memcpy(wrong,media,sizeof(wrong));
 wrong[1].snapshot_device=wrong[0].snapshot_device;
 wrong[1].snapshot_inode=wrong[0].snapshot_inode;
 assert(!cmp5_backup_group(root,wrong,3,private,"ALIAS",&b,NULL,NULL));
 memcpy(wrong,media,sizeof(wrong));
 wrong[0].media_type=3;
 wrong[1].media_type=3;
 assert(!cmp5_backup_group(root,wrong,3,private,"SIDECAR_ONLY",&b,NULL,NULL));
 memcpy(wrong,media,sizeof(wrong));
 strcpy(wrong[1].preview_path,wrong[0].preview_path);
 assert(!cmp5_backup_group(root,wrong,3,private,"SAME_EXT",&b,NULL,NULL));
 assert(!cmp5_backup_group(root,media,3,private,"BAD/ID",&b,NULL,NULL));
 assert(!cmp5_backup_group(root,media,1,private,"SINGLE",&b,NULL,NULL));
 Cmp5MediaCandidate invalid=media[0];
 /* Reject snapshots whose claimed media type does not match their suffix. */
 invalid.media_type=2;
 assert(!cmp5_plan_capture_group(root,&invalid,found,&found_count,NULL,NULL));
 assert(found_count==0);
 invalid=media[2];
 invalid.media_type=1;
 assert(!cmp5_plan_capture_group(root,&invalid,found,&found_count,NULL,NULL));
 assert(found_count==0);
 invalid=media[0];
 invalid.snapshot_inode++;
 assert(!cmp5_plan_capture_group(root,&invalid,found,&found_count,NULL,NULL));
 assert(found_count==0);
 char nonregular[512];
 (void)snprintf(nonregular,sizeof(nonregular),"%s",jxr);
 size_t basename_len=strlen(nonregular);
 assert(basename_len>4&&basename_len<sizeof(nonregular)-2);
 memcpy(nonregular+basename_len-4,".png",5);
 assert(symlink(jpg,nonregular)==0);
 assert(!cmp5_plan_capture_group(root,&media[0],found,&found_count,NULL,NULL));
 assert(found_count==0);
 for(unsigned i=0;i<CMP5_GROUP_MAX;i++)
  assert(found[i].preview_path[0]==0&&found[i].bytes==0);
 assert(unlink(nonregular)==0);
 assert(!cmp5_verify_group(private,"ALBUM__COMMIT",&b,NULL,NULL));
 char file[512];
 (void)snprintf(file,sizeof(file),"%s/ALBUM__2",restored);
 FILE*f=fopen(file,"r+b");assert(f);
 assert(fseek(f,13,SEEK_SET)==0);
 int old_byte=fgetc(f);assert(old_byte>=0);
 assert(fseek(f,13,SEEK_SET)==0);
 assert(fputc(old_byte^0x20,f)>=0);
 assert(fclose(f)==0);
 assert(!cmp5_verify_restored_group(restored,"ALBUM",&b,NULL,NULL));
 assert(cmp5_inspect_backup_restore_pair(private,"ALBUM",restored,
   "ALBUM",&pair)&&pair.state==CMP5_PAIR_INVALID);
 assert(cmp5_inspect_restored_group_recovery(restored,"ALBUM",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INVALID);
 (void)snprintf(file,sizeof(file),"%s/ALBUM__2",private);
 f=fopen(file,"r+b");assert(f);
 assert(fseek(f,68,SEEK_SET)==0);
 int original=fgetc(f);assert(original>=0);
 assert(fseek(f,68,SEEK_SET)==0);
 assert(fputc(original^0x80,f)>=0);
 assert(fclose(f)==0);
 assert(!cmp5_verify_group(private,"ALBUM",&b,NULL,NULL));
 assert(cmp5_inspect_group_recovery(private,"ALBUM",&recovery));
 assert(recovery.state==CMP5_RECOVERY_INVALID&&recovery.marker_present);
 assert(!cmp5_restore_group(private,"ALBUM",restored,&b,NULL,NULL));
 /* Even a newly created *unrelated* sibling invalidates a stale scan. */
 ScanMutation race={0};
 const char*last_slash=strrchr(jxr,'/');
 assert(last_slash);
 const size_t folder_len=(size_t)(last_slash-jxr);
 assert(folder_len+strlen("/UNRELATED.txt")<sizeof(race.late));
 memcpy(race.late,jxr,folder_len);
 strcpy(race.late+folder_len,"/UNRELATED.txt");
 assert(!cmp5_plan_capture_group(root,&media[0],found,&found_count,
  add_unrelated_during_scan,&race));
 assert(race.created&&found_count==0);
 for(unsigned i=0;i<CMP5_GROUP_MAX;i++)
  assert(found[i].preview_path[0]==0&&found[i].bytes==0);
 assert(unlink(race.late)==0);
 memset(&race,0,sizeof(race));
 memcpy(race.late,jxr,folder_len);
 strcpy(race.late+folder_len,"/UNRELATED.txt");
 assert(!cmp5_inspect_capture_companions(root,&media[0],&observed,
   add_unrelated_during_scan,&race));
 assert(race.created&&observed.failure==CMP5_COMPANION_FAILURE_CHANGED);
 assert(observed.state==CMP5_COMPANIONS_UNVERIFIABLE&&observed.files==0u);
 assert(unlink(race.late)==0);
 puts("Atomic multi-file capture group: backup, restore, recovery, scan-race PASS");
 return 0;
}
