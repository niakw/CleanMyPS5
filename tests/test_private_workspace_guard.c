#define _DEFAULT_SOURCE
#include "private_workspace_guard.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void){
 const char*invalid[]={"/","/user","/user/av_contents/photo/private",
  "/system/private","/system_ex/payloads","/data/etaHEN/private",
  "/data/pldmgr/payloads","relative","/tmp/../private",
  "/tmp/./private","/tmp//private","/tmp/private/"};
 for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
  assert(!cmp5_workspace_name_ok(invalid[i])||
         cmp5_workspace_forbidden(invalid[i])||
         strcmp(invalid[i],"/")==0);
  assert(cmp5_open_private_workspace(invalid[i])<0);
 }
 char base[]="/tmp/cmp5-workspace-XXXXXX";
 assert(mkdtemp(base));
 char private_path[PATH_MAX],link_path[PATH_MAX],nested[PATH_MAX];
 int n=snprintf(private_path,sizeof(private_path),"%s/private",base);
 assert(n>0&&(size_t)n<sizeof(private_path));
 n=snprintf(link_path,sizeof(link_path),"%s/alias",base);
 assert(n>0&&(size_t)n<sizeof(link_path));
 n=snprintf(nested,sizeof(nested),"%s/alias/private",base);
 assert(n>0&&(size_t)n<sizeof(nested));
 assert(mkdir(private_path,0700)==0);
 int fd=cmp5_open_private_workspace(private_path);
 assert(fd>=0);
 assert(close(fd)==0);
 assert(symlink(private_path,link_path)==0);
 assert(cmp5_open_private_workspace(link_path)<0);
 assert(unlink(link_path)==0);
 assert(symlink(base,link_path)==0);
 /* macOS /tmp and /var can themselves be canonical filesystem aliases.
  * Resolve those safely and verify the opened inode matches the input. */
 fd=cmp5_open_private_workspace(nested);
 assert(fd>=0);
 assert(close(fd)==0);
 assert(chmod(private_path,0770)==0);
 assert(cmp5_open_private_workspace(private_path)<0);
 assert(chmod(private_path,0700)==0);
 assert(unlink(link_path)==0);
 assert(rmdir(private_path)==0);
 assert(rmdir(base)==0);
 puts("Private workspace canonical roots, inode, and permission gate: PASS");
 return 0;
}
