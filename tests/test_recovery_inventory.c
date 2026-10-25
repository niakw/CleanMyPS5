#define _DEFAULT_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
/* Simulate a removable volume being replaced after its valid checkpoint
 * was observed but before the scanner rechecks the mount path. */
static char swap_parent[550];
static int swap_armed;
static int inject_readdir_error,inject_closedir_error;
static int readdir_error_pending,closedir_error_pending;
static int deny_request_once,deny_state_once,io_request_once;
static int test_openat(int dir,const char*name,int flags){
 if(io_request_once&&!strcmp(name,"request.bin")){
  io_request_once=0;
  errno=EIO;
  return -1;
 }
 if((deny_request_once&&!strcmp(name,"request.bin"))||
    (deny_state_once&&!strcmp(name,".e2a-state"))){
  deny_request_once=0;
  deny_state_once=0;
  errno=EACCES;
  return -1;
 }
 return openat(dir,name,flags);
}
static struct dirent* test_readdir(DIR*dir){
 struct dirent*entry=readdir(dir);
 if(entry&&!strcmp(entry->d_name,"CUSA54321")){
  if(inject_readdir_error){
   inject_readdir_error=0;
   readdir_error_pending=1;
  }
  if(inject_closedir_error){
   inject_closedir_error=0;
   closedir_error_pending=1;
  }
 }
 if(!entry&&readdir_error_pending){
  readdir_error_pending=0;
  errno=EIO;
 }
 if(swap_parent[0]){
  if(swap_armed){
   char displaced[600];
   assert(snprintf(displaced,sizeof(displaced),"%s-replaced",swap_parent)>0);
   assert(rename(swap_parent,displaced)==0);
   assert(mkdir(swap_parent,0700)==0);
   swap_parent[0]=0;
   swap_armed=0;
  }else if(entry&&!strcmp(entry->d_name,"CUSA54321"))swap_armed=1;
 }
 return entry;
}
static int test_closedir(DIR*dir){
 int rc=closedir(dir);
 if(rc==0&&closedir_error_pending){
  closedir_error_pending=0;
  errno=EIO;
  return -1;
 }
 return rc;
}
#define readdir test_readdir
#define closedir test_closedir
#define openat test_openat
#include "../src/native/recovery_inventory.h"
#undef openat
#undef closedir
#undef readdir
#include <stdlib.h>
static int cancel_immediately(void*ignored){
 (void)ignored;return 1;
}
static int cancel_after_observation(void*context){
 const Cmp5RecoveryInventory*results=(const Cmp5RecoveryInventory*)context;
 return results&&results->count!=0u;
}
static void create_dir(const char*root,const char*path){
 char full[512];
 int n=snprintf(full,sizeof(full),"%s%s",root,path);
 assert(n>0&&(size_t)n<sizeof(full));
 assert(mkdir(full,0700)==0);
}
static void set_text(const char*path,const char*text){
 FILE*f=fopen(path,"wb");assert(f);
 assert(fwrite(text,1,strlen(text),f)==strlen(text));
 assert(fclose(f)==0);
}
static void make_checkpoint(const char*root,const char*dest,
                            const char*title,const char*step){
 char path[512];
 int n=snprintf(path,sizeof(path),"%s%s/%s",root,dest,title);
 assert(n>0&&(size_t)n<sizeof(path));
 assert(mkdir(path,0700)==0);
 strncat(path,"/.e2a-state",sizeof(path)-strlen(path)-1);
 assert(mkdir(path,0700)==0);
 char req[550];snprintf(req,sizeof(req),"%s/request.bin",path);
 FILE*f=fopen(req,"wb");assert(f);
 char data[1024]={0};
 assert(fwrite(data,1,sizeof(data),f)==sizeof(data));
 assert(fclose(f)==0);
 char stage[550];snprintf(stage,sizeof(stage),"%s/stage",path);
 set_text(stage,step);
}
int main(void){
 char root[]="/tmp/cmp5-recovery-XXXXXX";
 assert(mkdtemp(root));
 create_dir(root,"/data");
 create_dir(root,"/data/homebrew");
 create_dir(root,"/mnt");
 create_dir(root,"/mnt/usb0");
 create_dir(root,"/mnt/usb0/homebrew");
 create_dir(root,"/mnt/ext0");
 create_dir(root,"/mnt/ext0/homebrew");
 make_checkpoint(root,"/data/homebrew","PPSA12345","pack");
 make_checkpoint(root,"/mnt/usb0/homebrew","CUSA54321","copy");
 make_checkpoint(root,"/mnt/ext0/homebrew","PPSA67890","verify");
 Cmp5RecoveryInventory results={0};
 assert(cmp5_scan_recovery_inventory(root,&results));
 assert(results.count==3&&!results.hidden);
 int pack=0,copy=0,verify=0;
 for(unsigned i=0;i<results.count;i++){
  if(results.entries[i].stage==CMP5_RECOVERY_PACK)pack++;
  if(results.entries[i].stage==CMP5_RECOVERY_COPY)copy++;
  if(results.entries[i].stage==CMP5_RECOVERY_VERIFY)verify++;
 }
 assert(pack==1&&copy==1&&verify==1);
 assert(!results.partial&&!results.unavailable);
 /* Cancel before scanning or after recording the first item. Neither
  * may publish a partial candidate list or stale coverage counters. */
 memset(&results,0xA5,sizeof(results));
 assert(!cmp5_scan_recovery_inventory_cancel(root,&results,
                                            cancel_immediately,NULL));
 assert(results.count==0u&&results.hidden==0u&&
        results.folders_checked==0u&&!results.partial);
 assert(!cmp5_scan_recovery_inventory_cancel(root,&results,
                                            cancel_after_observation,&results));
 assert(results.count==0u&&results.hidden==0u&&
        results.folders_checked==0u&&!results.partial);
 assert(cmp5_scan_recovery_inventory(root,&results));
 assert(results.count==3u&&!results.partial);
 /* A private checkpoint denied by the sandbox is unknown, not missing.
  * The unaffected volumes remain safely inventoried. */
 deny_request_once=1;
 assert(cmp5_scan_recovery_inventory(root,&results));
 assert(!deny_request_once);
 assert(results.count==2u&&results.partial);
 /* The parent checkpoint directory can itself be inaccessible under
  * 0700 when the native title and packer have different credentials. */
 deny_state_once=1;
 assert(cmp5_scan_recovery_inventory(root,&results));
 assert(!deny_state_once);
 assert(results.count==2u&&results.partial);
 /* A read-only I/O fault likewise cannot be reported as no job. */
 io_request_once=1;
 assert(cmp5_scan_recovery_inventory(root,&results));
 assert(!io_request_once);
 assert(results.count==2u&&results.partial);
 assert(cmp5_scan_recovery_inventory(root,&results));
 assert(results.count==3u&&!results.partial);

 /* Nonexistent removable volume is not an authorization error. */
 errno=0;
 assert(cmp5_recovery_parent_fd(root,"/mnt/usb7/homebrew")==-1);
 assert(errno==ENOENT);
 /* Changing one root must never erase valid results from other roots. */
 assert(snprintf(swap_parent,sizeof(swap_parent),
                 "%s/mnt/usb0/homebrew",root)>0);
 assert(cmp5_scan_recovery_inventory(root,&results));
 assert(results.partial&&results.count==2&&results.hidden==0);
 assert(!strcmp(results.entries[0].title_id,"PPSA12345"));
 assert(!strcmp(results.entries[1].title_id,"PPSA67890"));
 char original_parent[550],displaced[600];
 assert(snprintf(original_parent,sizeof(original_parent),
                 "%s/mnt/usb0/homebrew",root)>0);
 assert(snprintf(displaced,sizeof(displaced),
                 "%s-replaced",original_parent)>0);
 assert(rmdir(original_parent)==0);
 assert(rename(displaced,original_parent)==0);
 /* A read error after a valid record is observed invalidates that entire
  * volume, while other volumes retain their independently valid records. */
 inject_readdir_error=1;
 assert(cmp5_scan_recovery_inventory(root,&results));
 assert(!inject_readdir_error&&!readdir_error_pending);
 assert(results.partial&&results.count==2);
 assert(!strcmp(results.entries[0].title_id,"PPSA12345"));
 assert(!strcmp(results.entries[1].title_id,"PPSA67890"));
 /* Failure to close the directory is also an incomplete traversal. */
 inject_closedir_error=1;
 assert(cmp5_scan_recovery_inventory(root,&results));
 assert(!inject_closedir_error&&!closedir_error_pending);
 assert(results.partial&&results.count==2);
 assert(!strcmp(results.entries[0].title_id,"PPSA12345"));
 assert(!strcmp(results.entries[1].title_id,"PPSA67890"));
 /* Control: after injected I/O errors the complete scan recovers. */
 assert(cmp5_scan_recovery_inventory(root,&results));
 assert(results.count==3&&!results.partial);
 char name[550];
 snprintf(name,sizeof(name),"%s/mnt/ext0/homebrew/PPSA67890/.e2a-state/stage",root);
 assert(unlink(name)==0);
 assert(symlink("/etc/passwd",name)==0);
 assert(cmp5_scan_recovery_inventory(root,&results)&&results.count==2);
 assert(results.partial); /* Unsafe stage alias is not an absent job. */
 assert(unlink(name)==0);
 set_text(name,"unknown");
 assert(cmp5_scan_recovery_inventory(root,&results)&&results.count==2);
 assert(unlink(name)==0);
 set_text(name,"verify");
 snprintf(name,sizeof(name),"%s/data/homebrew/PPSA12345/.e2a-state/request.bin",root);
 assert(unlink(name)==0);
 assert(cmp5_scan_recovery_inventory(root,&results)&&results.count==2);
 assert(!cmp5_recovery_title("PPSA1234"));
 assert(!cmp5_recovery_title("PPSA1234X"));
 assert(!cmp5_recovery_title("../system"));
 assert(!cmp5_recovery_title("ppsa12345"));
 /* Test a symlinked destination root; never follow it into backups. */
 snprintf(name,sizeof(name),"%s/mnt/usb0/homebrew",root);
 assert(rename(name,strcat(strcpy((char[550]){0},name),"-renamed"))==0);
 assert(symlink("/data/homebrew",name)==0);
 errno=0;
 assert(cmp5_recovery_parent_fd(root,"/mnt/usb0/homebrew")==-1);
 assert(errno!=ENOENT);
 assert(cmp5_scan_recovery_inventory(root,&results));
 assert(results.count==1&&results.partial&&results.unavailable>=1u&&
        !strcmp(results.entries[0].title_id,"PPSA67890"));
 puts("CleanMyPS5 offline checkpoint inventory: PASS");
 return 0;
}
