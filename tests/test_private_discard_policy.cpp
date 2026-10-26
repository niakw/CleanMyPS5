// Compile the actual UI-facing C++ wrapper against deterministic C outcomes.
// Never opens host/PS5 files, and never calls the real unlinking engine.
// CMake Release sets NDEBUG, but fixtures must ALWAYS execute assertions.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../native/private_media_backup.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
using cmp5_private_backup::RehearsalStatus;
static unsigned attempts=0u;
static int mutation=0,success=0;
static std::uint64_t nominal=0u;
extern "C" int cmp5_discard_rehearsed_backup(
 const char*backup,const char*id,const char*dest,const char*name,
 uint64_t*reported,int*started,Cmp5CancelCheck,void*){
 assert(std::strcmp(backup,"/download0/cmp5-private-backups")==0);
 assert(std::strcmp(id,"CAP_0123456789ABCDEF")==0);
 assert(std::strcmp(dest,"/download0/cmp5-private-rehearsals")==0);
 assert(std::strcmp(name,"RAW_CAP_0123456789ABCDEF")==0);
 assert(reported&&started);
 ++attempts;
 *reported=nominal;
 *started=mutation;
 return success;
}
static int cancel_immediately(void*){return 1;}
int main(){
 using cmp5_private_backup::discard_rehearsal_by_id;
 const char*id="CAP_0123456789ABCDEF";
 assert(discard_rehearsal_by_id("GRP_0123456789ABCDEF",nullptr,nullptr).status==
   RehearsalStatus::denied);
 assert(discard_rehearsal_by_id(id,cancel_immediately,nullptr).status==
   RehearsalStatus::denied);
 assert(attempts==0u);
 mutation=0;success=0;nominal=0u;
 assert(discard_rehearsal_by_id(id,nullptr,nullptr).status==
   RehearsalStatus::failed);
 assert(attempts==1u);
 mutation=1;success=0;nominal=0u;
 assert(discard_rehearsal_by_id(id,nullptr,nullptr).status==
   RehearsalStatus::indeterminate);
 assert(attempts==2u);
 mutation=1;success=1;nominal=0u;
 assert(discard_rehearsal_by_id(id,nullptr,nullptr).status==
   RehearsalStatus::indeterminate);
 assert(attempts==3u);
 mutation=1;success=1;nominal=4096u;
 const auto result=discard_rehearsal_by_id(id,nullptr,nullptr);
 assert(result.status==RehearsalStatus::verified&&result.bytes==nominal);
 assert(attempts==4u);
 std::puts("PS5 private cleanup result tri-state wrapper: PASS");
 return 0;
}
