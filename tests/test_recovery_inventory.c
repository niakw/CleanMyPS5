#define _DEFAULT_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/native/recovery_inventory.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
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
 char name[550];
 snprintf(name,sizeof(name),"%s/mnt/ext0/homebrew/PPSA67890/.e2a-state/stage",root);
 assert(unlink(name)==0);
 assert(symlink("/etc/passwd",name)==0);
 assert(cmp5_scan_recovery_inventory(root,&results)&&results.count==2);
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
 assert(cmp5_scan_recovery_inventory(root,&results));
 assert(results.count==1&&
        !strcmp(results.entries[0].title_id,"PPSA67890"));
 puts("CleanMyPS5 offline checkpoint inventory: PASS");
 return 0;
}
