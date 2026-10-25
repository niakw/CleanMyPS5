// Read-only private inventory policy: real host directory fixtures only.
#include "../native/private_capsule_inventory.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <initializer_list>

static void make_file(const char*root,const char*name,mode_t mode,
                      std::size_t count=65u){
 char path[512]{};
 assert(std::snprintf(path,sizeof(path),"%s/%s",root,name)>0);
 const int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,mode);
 assert(fd>=0);
 unsigned char buf[2048]{};
 assert(count>0u&&count<=sizeof(buf));
 assert(write(fd,buf,count)==(ssize_t)count);
 assert(close(fd)==0);
 assert(chmod(path,mode)==0);
}
int main(){
 using namespace cmp5_private_backup;
 assert(valid_capsule_id("CAP_0123456789ABCDEF"));
 assert(!valid_capsule_id("CAP_0123456789ABCDEG"));
 assert(!valid_capsule_id("CAP_0123456789abcdef"));
 assert(!valid_capsule_id("CAP_0123456789ABCDEF/../"));
 assert(!valid_capsule_id("CAP_0000000000000000.part"));
 char area[]="/tmp/cmp5-capsules-XXXXXX";
 char*root=mkdtemp(area);
 assert(root!=nullptr);
 assert(chmod(root,0700)==0);
 Inventory list{};
 assert(scan_at(root,&list,nullptr,nullptr));
 assert(list.checked&&!list.partial&&list.count==0u);
 make_file(root,"CAP_0123456789ABCDEF",0600);
 assert(scan_at(root,&list,nullptr,nullptr));
 assert(list.checked&&!list.partial&&list.count==1u);
 assert(std::strcmp(list.ids[0],"CAP_0123456789ABCDEF")==0);
 make_file(root,".CAP_1111111111111111.part",0600);
 assert(scan_at(root,&list,nullptr,nullptr));
 assert(list.checked&&list.partial&&list.count==1u&&list.staged==1u);
 char stage[512]{};
 assert(std::snprintf(stage,sizeof(stage),"%s/.CAP_1111111111111111.part",root)>0);
 assert(unlink(stage)==0);
 char file[512]{};
 assert(std::snprintf(file,sizeof(file),"%s/CAP_0123456789ABCDEF",root)>0);
 assert(chmod(file,0644)==0);
 assert(scan_at(root,&list,nullptr,nullptr));
 assert(list.checked&&list.partial&&list.count==0u);
 assert(chmod(file,0600)==0);
 char linked[512]{};
 assert(std::snprintf(linked,sizeof(linked),"%s/CAP_FFFFFFFFFFFFFFFF",root)>0);
 assert(symlink(file,linked)==0);
 assert(scan_at(root,&list,nullptr,nullptr));
 assert(list.checked&&list.partial&&list.count==1u);
 assert(unlink(linked)==0);
 assert(unlink(file)==0);
 assert(valid_group_id("GRP_0123456789ABCDEF"));
 assert(!valid_group_id("GRP_0123456789ABCDEG"));
 bool commit=false,temporary=false;
 assert(group_entry_name("GRP_0123456789ABCDEF__COMMIT",
                          &commit,&temporary)&&commit&&!temporary);
 assert(group_entry_name(".GRP_0123456789ABCDEF__2.part",
                          &commit,&temporary)&&!commit&&temporary);
 assert(!group_entry_name("GRP_0123456789ABCDEF__5",
                          &commit,&temporary));
 make_file(root,"GRP_0123456789ABCDEF__COMMIT",0600,256u);
 make_file(root,"GRP_0123456789ABCDEF__ORIGINS",0600,1308u);
 make_file(root,"GRP_0123456789ABCDEF__1",0600,65u);
 assert(scan_at(root,&list,nullptr,nullptr));
 assert(list.checked&&!list.partial&&list.group_count==1u);
 assert(std::strcmp(list.group_ids[0],"GRP_0123456789ABCDEF")==0);
 assert(list.count==0u);
 make_file(root,".GRP_0123456789ABCDEF__2.part",0600);
 assert(scan_at(root,&list,nullptr,nullptr));
 assert(list.checked&&list.partial&&list.group_count==1u);
 assert(list.staged==1u);
 for(const char*name:{
  "GRP_0123456789ABCDEF__COMMIT",
  "GRP_0123456789ABCDEF__ORIGINS",
  "GRP_0123456789ABCDEF__1",
  ".GRP_0123456789ABCDEF__2.part"
 }){
  char entry[512]{};
  assert(std::snprintf(entry,sizeof(entry),"%s/%s",root,name)>0);
  assert(unlink(entry)==0);
 }
 assert(rmdir(root)==0);
 std::puts("Private native capsule inventory: read-only boundaries PASS");
}
