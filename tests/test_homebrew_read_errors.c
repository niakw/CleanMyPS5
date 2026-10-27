/* Deterministic POSIX directory I/O error regression in the real,
 * read-only homebrew scanner. Host fixture only, never console files. */
#define _DEFAULT_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static int eof_error_enabled,eof_error_seen;
static int close_error_enabled,close_error_seen;
static int fake_cache_identity_mode,cache_name_lookups;
static int fake_nested_identity_mode,nested_name_lookups;
static int fake_app_identity_mode,app_name_lookups;
static int fake_regular_mode,regular_name_lookups;
static int fake_directory_metadata_mode,directory_name_lookups;
static int fake_root_metadata_mode,root_name_lookups;
static ino_t fake_root_inode;
static int race_fstat(int fd,struct stat*st){
 const int rc=fstat(fd,st);
 if(rc==0&&fake_root_metadata_mode&&S_ISDIR(st->st_mode)&&
    st->st_ino==fake_root_inode){
  ++root_name_lookups;
  if(root_name_lookups==2)st->st_mtime+=60;
 }
 return rc;
}
/* Inject an identity swap at either lookup edge of /data/<app>/cache.
 * Actual files remain untouched; this simulates concurrent renames
 * reproducibly on macOS, Linux and under sanitizers. */
static int race_fstatat(int parent,const char*name,struct stat*st,int flags){
 const int rc=fstatat(parent,name,st,flags);
 if(rc==0&&fake_cache_identity_mode&&strcmp(name,"cache")==0){
  ++cache_name_lookups;
  if((fake_cache_identity_mode==1&&cache_name_lookups==1)||
     (fake_cache_identity_mode==2&&cache_name_lookups==2))
   st->st_ino+=1;
  if(fake_cache_identity_mode==3&&cache_name_lookups==1)
   st->st_dev+=1; /* simulated bind mount outside app device */
 }
 if(rc==0&&fake_nested_identity_mode&&strcmp(name,"nested")==0){
  ++nested_name_lookups;
  if(fake_nested_identity_mode==3&&nested_name_lookups==1)
   st->st_dev+=1; /* nested bind mount outside cache device */
  else if(nested_name_lookups==fake_nested_identity_mode)
   st->st_ino+=1;
 }
 if(rc==0&&fake_directory_metadata_mode){
  const char*target=fake_directory_metadata_mode==1?"cache":
                    fake_directory_metadata_mode==2?"nested":"ExampleTool";
  if(strcmp(name,target)==0){
   ++directory_name_lookups;
   if(directory_name_lookups==(fake_directory_metadata_mode==3?4:2))
    st->st_mtime+=60; /* same inode, modified contents in place */
  }
 }
 if(rc==0&&fake_regular_mode){
  const char*target=fake_regular_mode==3?"public.bin":
                    fake_regular_mode==4?"notice.txt":"index.bin";
  if(strcmp(name,target)==0){
   ++regular_name_lookups;
   if(regular_name_lookups==2){
    if(fake_regular_mode==1||fake_regular_mode==4)st->st_ino+=1;
    else if(fake_regular_mode==2)st->st_size+=1;
    else if(fake_regular_mode==3)st->st_mtime-=60;
   }
  }
 }
 if(rc==0&&fake_app_identity_mode&&strcmp(name,"ExampleTool")==0){
  ++app_name_lookups;
  /* /data/homebrew/ExampleTool is sampled before AND after its
   * homebrew traversal (lookups 1 and 2). The third stat precedes
   * opening the independent /data/ExampleTool app-data tree. */
  if((fake_app_identity_mode==1&&app_name_lookups==3)||
     (fake_app_identity_mode==2&&app_name_lookups==2)||
     (fake_app_identity_mode==3&&app_name_lookups==4))
   st->st_ino+=1;
 }
 return rc;
}
static struct dirent* race_readdir(DIR*dir){
 struct dirent*entry=readdir(dir);
 if(!entry&&eof_error_enabled&&!eof_error_seen){
  eof_error_seen=1;errno=EIO;
 }
 return entry;
}
static int race_closedir(DIR*dir){
 const int rc=closedir(dir);
 if(rc==0&&close_error_enabled&&!close_error_seen){
  close_error_seen=1;errno=EIO;return -1;
 }
 return rc;
}
#define readdir race_readdir
#define closedir race_closedir
#define fstatat race_fstatat
#define fstat race_fstat
#include "../src/native/homebrew.c"
#undef fstat
#undef fstatat
#undef closedir
#undef readdir
int main(int argc,char**argv){
 if(argc!=2)return 2;
 Cmp5HomebrewStats stats={0};
 size_t size=0;
 eof_error_enabled=1;
 char*json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
 assert(json&&size>100);
 assert(eof_error_seen&&stats.partial==1u&&stats.homebrew==2u);
 assert(stats.installed==1u&&stats.nominal_bytes>0u);
 assert(strstr(json,"\"partial\":true"));
 free(json);
 eof_error_enabled=0;close_error_enabled=1;close_error_seen=0;
 memset(&stats,0,sizeof(stats));size=0;
 json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
 assert(json&&size>100);
 assert(close_error_seen&&stats.partial==1u&&stats.homebrew==2u);
 assert(stats.installed==1u);
 assert(strstr(json,"\"partial\":true"));
 free(json);
 /* A cache name that resolves to a different inode must not
  * contribute its nominal bytes to an allegedly complete scan.
  * readdir order differs by host: either the 6-byte or 2-byte cache
  * directory can be the one replaced. */
 for(int mode=1;mode<=3;mode++){
  fake_cache_identity_mode=mode;cache_name_lookups=0;
  memset(&stats,0,sizeof(stats));size=0;
  json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
  assert(json&&size>100&&stats.partial&&stats.cache_like_partial);
  assert(cache_name_lookups>=2);
  assert(stats.cache_like_folders==3u);
  assert(stats.cache_like_name_matched+
         stats.cache_like_name_unmatched==stats.cache_like_folders);
  assert(stats.cache_like_nominal_bytes==13u||
         stats.cache_like_nominal_bytes==20u);
  assert(strstr(json,"\"cacheCandidates\":{\"folders\":3,"));
  assert(strstr(json,"\"providerVerified\":false,\"deletable\":false"));
  free(json);
 }
 /* Cache with a nested directory replaced at first or final lookup.
  * Scanner may retain a nominal observation but its timestamp MUST be
  * UNKNOWN (never "no recent activity") and the report partial. */
 for(int mode=1;mode<=3;mode++){
  fake_nested_identity_mode=mode;nested_name_lookups=0;
  memset(&stats,0,sizeof(stats));size=0;
  json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
  assert(json&&size>100&&stats.partial&&stats.cache_like_partial);
  assert(nested_name_lookups>=(mode==3?1:mode));
  assert(stats.cache_like_folders==4u);
  /* Three bytes in the nested subtree must be discarded, not just
   * flagged as an untrusted timestamp. */
  assert(stats.cache_like_nominal_bytes==19u&&
         stats.cache_like_files==4u);
  assert(stats.cache_mtime_unknown==2u);
  assert(stats.cache_mtime_recent==0u);
  assert(stats.cache_mtime_no_recent==2u);
  assert(stats.cache_mtime_recent+stats.cache_mtime_unknown+
         stats.cache_mtime_no_recent==stats.cache_like_folders);
  assert(strstr(json,"\"idleVerified\":false,\"providerVerified\":false,\"deletable\":false"));
  free(json);
 }
 fake_nested_identity_mode=0;
 /* A case-only label difference is not an identity match, even on
  * macOS's default case-insensitive filesystem. Test pure comparison
  * without creating filesystem aliases. */
 Audit mock={0};
 mock.homebrew_name_count=1u;
 strcpy(mock.homebrew_names[0],"ExampleTool");
 assert(homebrew_name_observed(&mock,"ExampleTool"));
 assert(!homebrew_name_observed(&mock,"exampletool"));
 assert(!homebrew_name_observed(&mock,"ExampleToolOld"));
 fake_cache_identity_mode=0;
 fake_app_identity_mode=1;app_name_lookups=0;
 memset(&stats,0,sizeof(stats));size=0;
 json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
 assert(json&&size>100&&stats.partial);
 assert(app_name_lookups==3); /* mismatched open stops before post-scan */
 /* A swapped top-level /data/<app> must never contribute cache bytes. */
 assert(stats.cache_like_folders==1u&&stats.cache_like_nominal_bytes==2u);
 assert(stats.cache_like_name_matched==0u&&
        stats.cache_like_name_unmatched==1u);
 assert(stats.cache_mtime_recent==0u&&stats.cache_mtime_unknown==0u&&
        stats.cache_mtime_no_recent==1u);
 assert(strstr(json,"\"partial\":true"));
 free(json);
 /* The source /data/homebrew/ExampleTool directory is now replaced
  * during final lookup. Its name MUST be removed from weak owner hints,
  * even though the matching /data/ExampleTool cache still exists. */
 fake_app_identity_mode=2;app_name_lookups=0;
 memset(&stats,0,sizeof(stats));size=0;
 json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
 assert(json&&size>100&&stats.partial);
 assert(app_name_lookups>=4);
 /* 3 bytes from the stale /data/homebrew/ExampleTool inode must not
  * survive in global nominal bytes, top-N or the JSON item. */
 assert(stats.nominal_bytes==36u);
 assert(strstr(json,"\"path\":\"/data/homebrew/ExampleTool\",\"kind\":\"homebrew\",\"risk\":\"protected\",\"nominalBytes\":0,\"files\":0,\"partial\":true"));
 assert(stats.cache_like_folders==4u);
 assert(stats.cache_like_name_matched==0u&&
        stats.cache_like_name_unmatched==4u);
 assert(stats.cache_mtime_recent==1u&&stats.cache_mtime_unknown==1u&&
        stats.cache_mtime_no_recent==2u);
 assert(stats.cache_mtime_recent+stats.cache_mtime_unknown+
        stats.cache_mtime_no_recent==stats.cache_like_folders);
 assert(strstr(json,"\"providerVerified\":false,\"deletable\":false"));
 free(json);
 /* Fourth lookup is the final /data/ExampleTool stat. This time the
  * directory was fully traversed, including the cache shortlist,
  * before it was replaced. All its 19 nominal bytes and five
  * general files must disappear from stale parent accounting. */
 fake_app_identity_mode=3;app_name_lookups=0;
 memset(&stats,0,sizeof(stats));size=0;
 json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
 assert(json&&size>100&&stats.partial&&stats.cache_like_partial);
 assert(app_name_lookups>=4);
 assert(stats.nominal_bytes==20u);
 assert(stats.cache_like_folders==1u&&stats.cache_like_nominal_bytes==2u);
 assert(stats.cache_like_name_matched==0u&&stats.cache_like_name_unmatched==1u);
 assert(stats.cache_mtime_no_recent==1u&&
        stats.cache_mtime_recent==0u&&stats.cache_mtime_unknown==0u);
 assert(strstr(json,"\"path\":\"/data/ExampleTool\",\"kind\":\"app-data\",\"risk\":\"protected\",\"nominalBytes\":0,\"files\":0,\"partial\":true"));
 free(json);
 fake_app_identity_mode=0;
 /* A metadata-only scanner must not count an inode, length or mtime
  * which changed between its two anchored file-name probes. */
 for(int mode=1;mode<=4;mode++){
  fake_regular_mode=mode;regular_name_lookups=0;
  memset(&stats,0,sizeof(stats));size=0;
  json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
  assert(json&&size>100&&stats.partial);
  assert(regular_name_lookups>=2);
  if(mode<=2){
   /* index.bin is counted separately in the cache shortlist first.
    * Later clean metadata samples in the general tree are independent. */
   assert(stats.cache_like_partial);
   assert(stats.cache_like_folders==4u);
   assert(stats.cache_like_nominal_bytes==16u&&stats.cache_like_files==4u);
   assert(stats.cache_mtime_recent==0u&&stats.cache_mtime_unknown==2u&&
          stats.cache_mtime_no_recent==2u);
   assert(stats.nominal_bytes==39u);
  }else if(mode==3){
   assert(stats.nominal_bytes==35u);
   assert(!stats.cache_like_partial&&stats.cache_like_nominal_bytes==22u);
   assert(strstr(json,"\"path\":\"/data/ExampleTool\",\"kind\":\"app-data\",\"risk\":\"protected\",\"nominalBytes\":15,\"files\":4,\"partial\":true"));
  }else{
   assert(stats.nominal_bytes==36u);
   assert(!stats.cache_like_partial&&stats.cache_like_nominal_bytes==22u);
   assert(strstr(json,"\"path\":\"/data/etaHEN/notice.txt\",\"kind\":\"app-data\",\"risk\":\"protected\",\"nominalBytes\":0,\"files\":0,\"partial\":true"));
  }
  assert(strstr(json,"\"partial\":true"));
  assert(strstr(json,"\"deletable\":false"));
  free(json);
 }
 fake_regular_mode=0;
 /* Parent-directory changes must not preserve stale file counts even
  * if dev/inode remain identical at every sampled lookup. */
 for(int mode=1;mode<=3;mode++){
  fake_directory_metadata_mode=mode;directory_name_lookups=0;
  memset(&stats,0,sizeof(stats));size=0;
  json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
  assert(json&&size>100&&stats.partial);
  assert(directory_name_lookups>=(mode==3?4:2));
  if(mode==1){
   assert(stats.cache_like_partial);
   assert(stats.cache_like_folders==3u);
   /* Which cache named directory is sampled first depends on the
    * filesystem's readdir order: 9 bytes / 2 files for ExampleTool,
    * or 2 bytes / 1 file for OtherApp. Both must disappear completely. */
   assert((stats.cache_like_nominal_bytes==13u&&stats.cache_like_files==3u)||
          (stats.cache_like_nominal_bytes==20u&&stats.cache_like_files==4u));
   assert(stats.nominal_bytes==39u);
  }else if(mode==2){
   assert(stats.cache_like_partial);
   assert(stats.cache_like_folders==4u);
   assert(stats.cache_like_nominal_bytes==19u&&stats.cache_like_files==4u);
   assert(stats.cache_mtime_unknown==2u);
   assert(stats.nominal_bytes==39u);
  }else{
   assert(stats.cache_like_partial);
   assert(stats.cache_like_folders==1u&&stats.cache_like_nominal_bytes==2u);
   assert(stats.nominal_bytes==20u);
   assert(strstr(json,"\"path\":\"/data/ExampleTool\",\"kind\":\"app-data\",\"risk\":\"protected\",\"nominalBytes\":0,\"files\":0,\"partial\":true"));
  }
  assert(strstr(json,"\"deletable\":false"));
  free(json);
 }
 fake_directory_metadata_mode=0;
 /* Simulate an in-place root mutation after its children were read.
  * Revalidating the root must discard all of THAT root's output,
  * not merely set partial=true on stale bytes or stale weak matches. */
 for(int mode=1;mode<=2;mode++){
  char name[1024];
  snprintf(name,sizeof(name),"%s/%s",argv[1],
           mode==1?"data/homebrew":"data");
  struct stat chosen={0};
  assert(stat(name,&chosen)==0&&S_ISDIR(chosen.st_mode));
  fake_root_inode=chosen.st_ino;
  fake_root_metadata_mode=mode;
  root_name_lookups=0;
  memset(&stats,0,sizeof(stats));size=0;
  json=cmp5_homebrew_with_stats(argv[1],&size,&stats,NULL,NULL);
  assert(json&&size>100&&stats.partial&&root_name_lookups>=2);
  if(mode==1){
   assert(stats.homebrew==0u&&stats.entries==5u);
   assert(stats.nominal_bytes==36u);
   assert(stats.cache_like_folders==4u);
   assert(stats.cache_like_name_matched==0u);
   assert(stats.cache_like_name_unmatched==4u);
   assert(!strstr(json,"\"path\":\"/data/homebrew/ExampleTool\""));
  }else{
   assert(stats.data_root_state==4u&&stats.entries==5u);
   assert(stats.nominal_bytes==18u);
   assert(stats.cache_like_folders==0u&&stats.cache_like_files==0u);
   assert(stats.cache_like_nominal_bytes==0u);
   assert(strstr(json,"\"appData\":\"error\""));
   assert(!strstr(json,"\"path\":\"/data/ExampleTool\""));
   assert(strstr(json,"\"path\":\"/data/etaHEN/notice.txt\""));
  }
  assert(strstr(json,"\"partial\":true"));
  assert(strstr(json,"\"deletable\":false"));
  free(json);
 }
 fake_root_metadata_mode=0;
 puts("Homebrew readdir and closedir failures plus cache identity races: PARTIAL; healthy apps retained");
 return 0;
}
