#define _DEFAULT_SOURCE
#include "diagnostics.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition) do { if(!(condition)) { \
 fprintf(stderr,"fixture check failed: %s at line %d\\n",#condition,__LINE__); \
 exit(1); } } while(0)
static void join_path(char*out,size_t cap,const char*parent,const char*suffix){
 const size_t base=strlen(parent),tail=strlen(suffix);
 CHECK(base+tail<cap);
 memcpy(out,parent,base);
 memcpy(out+base,suffix,tail+1);
}
static void make_dir(const char*path){
 CHECK(mkdir(path,0700)==0);
}
static void write_fixture(const char*path,const char*body){
 FILE*f=fopen(path,"wb");CHECK(f);
 const size_t n=strlen(body);
 CHECK(fwrite(body,1,n,f)==n);
 CHECK(fclose(f)==0);
}
static int cancel_at(void*context){
 int*counter=(int*)context;return ++*counter>=8;
}
static int always_cancel(void*context){(void)context;return 1;}
int main(void){
 char root[]="/tmp/cmp5-autoload-stats-XXXXXX";
 CHECK(mkdtemp(root));
 char path[512],data[512],base[512],pldmgr[512],eta[512],usb[512];
 join_path(data,sizeof(data),root,"/data");make_dir(data);
 join_path(base,sizeof(base),data,"/ps5_autoloader");make_dir(base);
 join_path(pldmgr,sizeof(pldmgr),data,"/pldmgr");make_dir(pldmgr);
 join_path(eta,sizeof(eta),data,"/etaHEN");make_dir(eta);
 join_path(path,sizeof(path),base,"/autoload.txt");
 write_fixture(path,"# not active\netaHEN.elf\n");
 join_path(path,sizeof(path),pldmgr,"/autoload.txt");
 write_fixture(path,"# only a comment\n");
 join_path(path,sizeof(path),eta,"/config.ini");
 write_fixture(path,"toolbox_auto_start = 1\nPRIVATE=NEVER_EXPORT\n");
 join_path(usb,sizeof(usb),root,"/mnt");make_dir(usb);
 char mount[512];join_path(mount,sizeof(mount),usb,"/usb0");make_dir(mount);
 join_path(path,sizeof(path),mount,"/ps5_autoloader");
 CHECK(symlink(base,path)==0);
 join_path(mount,sizeof(mount),usb,"/usb1");make_dir(mount);
 join_path(path,sizeof(path),mount,"/ps5_autoloader");make_dir(path);
 char huge[512];join_path(huge,sizeof(huge),path,"/autoload.txt");
 FILE*f=fopen(huge,"wb");CHECK(f);
 char block[1024];memset(block,'X',sizeof(block));
 for(unsigned i=0;i<65;i++)CHECK(fwrite(block,1,sizeof(block),f)==sizeof(block));
 CHECK(fclose(f)==0);
 size_t len=0;
 Cmp5AutoloadStats stats={0};
 char*json=cmp5_diagnostics_with_stats(root,&len,&stats);
 CHECK(json&&len==strlen(json));
 CHECK(stats.checked==11&&stats.present==5&&stats.missing==6);
 CHECK(stats.etahen_references==1&&stats.other_references==0);
 CHECK(stats.autostart_enabled==1&&stats.autostart_disabled==0);
 CHECK(stats.no_reference==1&&stats.unreadable==1&&stats.oversized==1);
 CHECK(stats.unknown==0);
 CHECK(strstr(json,"etaHEN-reference")&&strstr(json,"too-large"));
 CHECK(strstr(json,"unreadable")&&!strstr(json,"PRIVATE=NEVER_EXPORT"));
 free(json);
 size_t compatibility_len=0;
 char*legacy=cmp5_diagnostics(root,&compatibility_len);
 CHECK(legacy&&compatibility_len==len);
 free(legacy);
 len=55;memset(&stats,0xA5,sizeof(stats));
 CHECK(!cmp5_diagnostics_with_stats_cancel(
  root,&len,&stats,always_cancel,NULL));
 CHECK(len==0);
 Cmp5AutoloadStats zero={0};
 CHECK(memcmp(&stats,&zero,sizeof(stats))==0);
 int calls=0;
 len=123;memset(&stats,0x5A,sizeof(stats));
 CHECK(!cmp5_diagnostics_with_stats_cancel(
  root,&len,&stats,cancel_at,&calls));
 CHECK(calls>=8&&len==0);
 CHECK(memcmp(&stats,&zero,sizeof(stats))==0);
 len=0;
 json=cmp5_diagnostics_with_stats_cancel(root,&len,&stats,NULL,NULL);
 CHECK(json&&len==strlen(json)&&stats.checked==11);
 free(json);
 puts("Native autoload aggregate, cancellation and helper compatibility: PASS");
 return 0;
}
