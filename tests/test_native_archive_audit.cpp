// Host-only end-to-end audit: real SHA-256 capsules + grouped manifest.
// No Sony Gallery mutations and no PS5 hardware required.
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "../native/private_capsule_inventory.hpp"
#include "capture_companions.h"
#include "media_snapshot.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace fs=std::filesystem;
static void write_bytes(const fs::path&path,const std::string&body){
 std::ofstream file(path,std::ios::binary);
 assert(file.good());
 file<<body;
 file.close();
 assert(file.good());
}
int main(){
 using namespace cmp5_private_backup;
 char tmp[]="/tmp/cmp5-bulk-audit-XXXXXX";
 char*made=mkdtemp(tmp);
 assert(made);
 const fs::path root(made);
 const fs::path gallery=root/"user/av_contents/photo/NPXS00000/GAME123";
 const fs::path thumbnails=root/"user/av_contents/thumbnails/photo/NPXS00000/GAME123";
 fs::create_directories(gallery);
 fs::create_directories(thumbnails);
 const fs::path photo=gallery/"REC_001.jpg";
 write_bytes(photo,std::string(256u*1024u,'A'));
 write_bytes(gallery/"REC_001.dat","SYNTHETIC-DAT-METADATA");
 write_bytes(thumbnails/"REC_001.jpg.jpeg","SYNTHETIC-JPEG-THUMBNAIL");
 const fs::path store=root/"private-archives";
 fs::create_directory(store);
 assert(chmod(store.c_str(),0700)==0);
 struct stat st{};
 assert(stat(photo.c_str(),&st)==0);
 Cmp5MediaCandidate primary{};
 std::strcpy(primary.preview_path,
  "/user/av_contents/photo/NPXS00000/GAME123/REC_001.jpg");
 primary.media_type=1u;
 primary.bytes=(std::uint64_t)st.st_size;
 cmp5_snapshot_capture(&primary,&st);
 Cmp5BackupReceipt single{};
 constexpr const char*cap_id="CAP_0123456789ABCDEF";
 constexpr const char*group_id="GRP_0123456789ABCDEF";
 assert(cmp5_backup_media(root.c_str(),&primary,store.c_str(),cap_id,
                          &single,nullptr,nullptr));
 assert(single.published==1&&single.bytes==primary.bytes);
 Cmp5MediaCandidate members[CMP5_GROUP_MAX]{};
 std::uint32_t count=0;
 assert(cmp5_plan_capture_group(root.c_str(),&primary,members,&count,
                                nullptr,nullptr));
 assert(count==3u);
 Cmp5GroupReceipt backup_group{};
 assert(cmp5_backup_group(root.c_str(),members,count,store.c_str(),group_id,
                          &backup_group,nullptr,nullptr));
 ArchiveAudit audit_result{};
 assert(audit_at(store.c_str(),&audit_result,nullptr,nullptr));
 assert(audit_result.checked&&!audit_result.partial&&
        audit_result.capsules==1u&&audit_result.valid_capsules==1u&&
        audit_result.groups==1u&&audit_result.valid_groups==1u);
 assert(audit_result.verified_bytes==single.bytes+backup_group.bytes);

 // Tamper with the private single-copy payload, but leave its group
 // counterpart intact: the audit must count one INVALID and one VALID.
 int fd=open((store/cap_id).c_str(),O_RDWR|O_NOFOLLOW);
 assert(fd>=0);
 unsigned char value=0;
 assert(pread(fd,&value,1,90)==1);
 value^=1u;
 assert(pwrite(fd,&value,1,90)==1);
 assert(close(fd)==0);
 assert(audit_at(store.c_str(),&audit_result,nullptr,nullptr));
 assert(audit_result.checked&&!audit_result.partial&&
        audit_result.capsules==1u&&!audit_result.valid_capsules&&
        audit_result.groups==1u&&audit_result.valid_groups==1u);
 assert(audit_result.verified_bytes==backup_group.bytes);

 // An interrupted file limits coverage, without inventing extra validity.
 const fs::path part=store/".CAP_1111111111111111.part";
 write_bytes(part,"INCOMPLETE-PRIVATE-STAGE");
 assert(chmod(part.c_str(),0600)==0);
 assert(audit_at(store.c_str(),&audit_result,nullptr,nullptr));
 assert(audit_result.checked&&audit_result.partial);
 assert(audit_result.valid_groups==1u&&!audit_result.valid_capsules);

 const auto stop=[](void*)->int{return 1;};
 audit_result.checked=true;
 assert(!audit_at(store.c_str(),&audit_result,stop,nullptr));
 assert(!audit_result.checked&&!audit_result.verified_bytes);
 assert(fs::exists(photo));
 fs::remove_all(root);
 std::puts("Native bulk SHA-256 archive audit: corruption/partial/cancel PASS");
}
