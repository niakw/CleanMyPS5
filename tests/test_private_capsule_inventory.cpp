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
 char cap_unterminated[21]{};
 std::memcpy(cap_unterminated,"CAP_",4u);
 std::memset(cap_unterminated+4,'A',17u);
 assert(!valid_capsule_id(cap_unterminated));
 char grp_unterminated[21]{};
 std::memcpy(grp_unterminated,"GRP_",4u);
 std::memset(grp_unterminated+4,'A',17u);
 assert(!valid_group_id(grp_unterminated));
 /* An audit cannot reuse its summary after an unrelated archive
  * appears, a stage is added, or the bounded namespace goes partial. */
 Inventory first{},second{};
 first.checked=second.checked=true;
 first.count=second.count=2u;
 first.group_count=second.group_count=1u;
 std::strcpy(first.ids[0],"CAP_0123456789ABCDEF");
 std::strcpy(first.ids[1],"CAP_FFFFFFFFFFFFFFFF");
 std::strcpy(first.group_ids[0],"GRP_0123456789ABCDEF");
 std::strcpy(second.ids[0],first.ids[1]);
 std::strcpy(second.ids[1],first.ids[0]);
 std::strcpy(second.group_ids[0],first.group_ids[0]);
 assert(same_archive_listing(first,second));
 second.staged=1u;
 assert(!same_archive_listing(first,second));
 second.staged=0u;second.staged_nominal_bytes=65u;
 assert(!same_archive_listing(first,second));
 second.staged_nominal_bytes=0u;second.staged_unverified=1u;
 assert(!same_archive_listing(first,second));
 second.staged_unverified=0u;second.partial=true;
 assert(!same_archive_listing(first,second));
 second.partial=false;
 std::strcpy(second.ids[0],"CAP_0000000000000000");
 assert(!same_archive_listing(first,second));
 second=first;second.checked=false;
 assert(!same_archive_listing(first,second));
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
 /* An ID created later but sorted earlier must be displayed first,
  * regardless of the filesystem's native readdir order. */
 make_file(root,"CAP_0000000000000000",0600);
 assert(scan_at(root,&list,nullptr,nullptr));
 assert(list.count==2u);
 assert(std::strcmp(list.ids[0],"CAP_0000000000000000")==0);
 assert(std::strcmp(list.ids[1],"CAP_0123456789ABCDEF")==0);
 char earlier[512]{};
 assert(std::snprintf(earlier,sizeof(earlier),
                      "%s/CAP_0000000000000000",root)>0);
 assert(unlink(earlier)==0);
 make_file(root,".CAP_1111111111111111.part",0600);
 assert(scan_at(root,&list,nullptr,nullptr));
 assert(list.checked&&list.partial&&list.count==1u&&list.staged==1u);
 assert(list.staged_nominal_bytes==65u&&list.staged_unverified==0u);
 char stage[512]{};
 assert(std::snprintf(stage,sizeof(stage),"%s/.CAP_1111111111111111.part",root)>0);
 assert(unlink(stage)==0);
 /* A symbolic-link stage may be counted as an unknown residue, but must
  * never contribute the bytes of its external target to local storage. */
 assert(symlink("/etc/passwd",stage)==0);
 assert(scan_at(root,&list,nullptr,nullptr));
 assert(list.staged==1u&&list.staged_unverified==1u&&
        list.staged_nominal_bytes==0u);
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
 assert(group_entry_name(".GRP_0123456789ABCDEF__COMMIT.part",
                          &commit,&temporary)&&!commit&&temporary);
 assert(group_entry_name(".GRP_0123456789ABCDEF__ORIGINS.part",
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
 make_file(root,".GRP_0123456789ABCDEF__COMMIT.part",0600);
 make_file(root,".GRP_0123456789ABCDEF__ORIGINS.part",0600);
 assert(scan_at(root,&list,nullptr,nullptr));
 assert(list.checked&&list.partial&&list.group_count==1u);
 assert(list.staged==3u&&list.staged_nominal_bytes==195u&&
        list.staged_unverified==0u);
 for(const char*name:{
  "GRP_0123456789ABCDEF__COMMIT",
  "GRP_0123456789ABCDEF__ORIGINS",
  "GRP_0123456789ABCDEF__1",
  ".GRP_0123456789ABCDEF__2.part",
  ".GRP_0123456789ABCDEF__COMMIT.part",
  ".GRP_0123456789ABCDEF__ORIGINS.part"
 }){
  char entry[512]{};
  assert(std::snprintf(entry,sizeof(entry),"%s/%s",root,name)>0);
  assert(unlink(entry)==0);
 }
 /* Rename the very workspace being enumerated, then replace its path
  * with an empty 0700 directory. The held fd can still read the old one;
  * its entries must NEVER be published as belonging to the new path. */
 make_file(root,"CAP_ABCDEF0123456789",0600);
 struct Swap {
  const char*path;
  char displaced[512]{};
  unsigned calls=0u;
  bool changed=false;
 } swap{root};
 const auto replace_during_scan=[](void*context)->int{
  auto*state=static_cast<Swap*>(context);
  if(++state->calls==3u){
   assert(std::snprintf(state->displaced,sizeof(state->displaced),
                         "%s-moved",state->path)>0);
   assert(rename(state->path,state->displaced)==0);
   assert(mkdir(state->path,0700)==0);
   state->changed=true;
  }
  return 0;
 };
 list.checked=true;list.partial=true;
 list.count=1u;list.group_count=1u;list.staged=1u;
 assert(!scan_at(root,&list,replace_during_scan,&swap));
 assert(swap.changed);
 assert(!list.checked&&!list.partial&&!list.count&&!list.group_count&&
        !list.staged&&!list.staged_unverified&&!list.staged_nominal_bytes);
 /* The new directory contains no old capsule; the old one is intact.
  * Neither directory nor capsule may be silently removed by the scan. */
 assert(scan_at(root,&list,nullptr,nullptr));
 assert(list.checked&&!list.partial&&!list.count&&!list.group_count);
 /* In-place modification is not a rename of the directory. Its inode
  * remains the same while its contents and mtime/ctime change. Reject
  * instead of presenting a potentially incomplete catalogue. */
 make_file(root,"CAP_2222222222222222",0600);
 struct Mutation {
  const char*path;
  unsigned calls=0u;
  bool changed=false;
 } mutation{root};
 const auto add_during_scan=[](void*context)->int{
  auto*state=static_cast<Mutation*>(context);
  if(++state->calls==3u){
   make_file(state->path,"CAP_3333333333333333",0600);
   state->changed=true;
  }
  return 0;
 };
 list.checked=true;list.count=9u;list.group_count=3u;
 assert(!scan_at(root,&list,add_during_scan,&mutation));
 assert(mutation.changed);
 assert(!list.checked&&!list.partial&&!list.count&&!list.group_count);
 assert(scan_at(root,&list,nullptr,nullptr));
 assert(list.checked&&!list.partial&&list.count==2u);
 for(const char*name:{"CAP_2222222222222222","CAP_3333333333333333"}){
  char path[512]{};
  assert(std::snprintf(path,sizeof(path),"%s/%s",root,name)>0);
  assert(unlink(path)==0);
 }
 assert(rmdir(root)==0);
 char old_capsule[550]{};
 assert(std::snprintf(old_capsule,sizeof(old_capsule),
                      "%s/CAP_ABCDEF0123456789",swap.displaced)>0);
 struct stat intact{};
 assert(stat(old_capsule,&intact)==0&&S_ISREG(intact.st_mode));
 assert(unlink(old_capsule)==0);
 assert(rmdir(swap.displaced)==0);
 std::puts("Private native capsule inventory: read-only boundaries PASS");
}
