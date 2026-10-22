#define _DEFAULT_SOURCE
#include "diagnostics.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void make_dir(const char*path){
 assert(mkdir(path,0700)==0);
}
static void write_fixture(const char*path,const char*body){
 FILE*f=fopen(path,"wb");assert(f);
 const size_t n=strlen(body);
 assert(fwrite(body,1,n,f)==n);
 assert(fclose(f)==0);
}
int main(void){
 char root[]="/tmp/cmp5-autoload-stats-XXXXXX";
 assert(mkdtemp(root));
 char path[512],data[512],base[512],pldmgr[512],eta[512],usb[512];
 (void)snprintf(data,sizeof(data),"%s/data",root);make_dir(data);
 (void)snprintf(base,sizeof(base),"%s/ps5_autoloader",data);make_dir(base);
 (void)snprintf(pldmgr,sizeof(pldmgr),"%s/pldmgr",data);make_dir(pldmgr);
 (void)snprintf(eta,sizeof(eta),"%s/etaHEN",data);make_dir(eta);
 (void)snprintf(path,sizeof(path),"%s/autoload.txt",base);
 write_fixture(path,"# not active\netaHEN.elf\n");
 (void)snprintf(path,sizeof(path),"%s/autoload.txt",pldmgr);
 write_fixture(path,"# only a comment\n");
 (void)snprintf(path,sizeof(path),"%s/config.ini",eta);
 write_fixture(path,"toolbox_auto_start = 1\nPRIVATE=NEVER_EXPORT\n");
 (void)snprintf(usb,sizeof(usb),"%s/mnt",root);make_dir(usb);
 char mount[512];(void)snprintf(mount,sizeof(mount),"%s/usb0",usb);make_dir(mount);
 (void)snprintf(path,sizeof(path),"%s/ps5_autoloader",mount);
 assert(symlink(base,path)==0);
 (void)snprintf(mount,sizeof(mount),"%s/usb1",usb);make_dir(mount);
 (void)snprintf(path,sizeof(path),"%s/ps5_autoloader",mount);make_dir(path);
 char huge[512];(void)snprintf(huge,sizeof(huge),"%s/autoload.txt",path);
 FILE*f=fopen(huge,"wb");assert(f);
 char block[1024];memset(block,'X',sizeof(block));
 for(unsigned i=0;i<65;i++)assert(fwrite(block,1,sizeof(block),f)==sizeof(block));
 assert(fclose(f)==0);
 size_t len=0;
 Cmp5AutoloadStats stats={0};
 char*json=cmp5_diagnostics_with_stats(root,&len,&stats);
 assert(json&&len==strlen(json));
 assert(stats.checked==11&&stats.present==5&&stats.missing==6);
 assert(stats.etahen_references==1&&stats.other_references==0);
 assert(stats.autostart_enabled==1&&stats.autostart_disabled==0);
 assert(stats.no_reference==1&&stats.unreadable==1&&stats.oversized==1);
 assert(stats.unknown==0);
 assert(strstr(json,"etaHEN-reference")&&strstr(json,"too-large"));
 assert(strstr(json,"unreadable")&&!strstr(json,"PRIVATE=NEVER_EXPORT"));
 free(json);
 size_t compatibility_len=0;
 char*legacy=cmp5_diagnostics(root,&compatibility_len);
 assert(legacy&&compatibility_len==len);
 free(legacy);
 puts("Native autoload aggregate and helper backward compatibility: PASS");
 return 0;
}
