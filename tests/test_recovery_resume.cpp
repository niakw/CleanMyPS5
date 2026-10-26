// Bounded and side-effect-free recovery policy tests (no PS5 access).
#include "../native/recovery_resume.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
int main(){
 using namespace cmp5_compression;
 Cmp5RecoveryCandidate selected{};
 std::strcpy(selected.title_id,"PPSA12345");
 std::strcpy(selected.destination,"/mnt/usb0/homebrew");
 selected.stage=CMP5_RECOVERY_PACK;
 Cmp5RecoveryInventory inventory{};
 inventory.count=1;
 inventory.entries[0]=selected;
 auto scan=[&](const char*root,Cmp5RecoveryInventory*out)->bool{
  assert(!std::strcmp(root,"/"));
  *out=inventory;return true;
 };
 assert(valid_recovery_candidate(selected));
 assert(recovery_candidate_fresh(selected,scan));
 /* Another inaccessible device is not proof this selected checkpoint
  * is unsafe: the scanner retains independently verified entries. */
 inventory.partial=1;
 assert(recovery_candidate_fresh(selected,scan));
 inventory.partial=0;inventory.hidden=1;
 assert(recovery_candidate_fresh(selected,scan));
 inventory.hidden=0;
 inventory.entries[0].stage=CMP5_RECOVERY_COPY;
 assert(!recovery_candidate_fresh(selected,scan));
 inventory.entries[0]=selected;
 std::strcpy(inventory.entries[0].title_id,"PPSA98765");
 assert(!recovery_candidate_fresh(selected,scan));
 inventory.entries[0]=selected;
 auto failed=[](const char*,Cmp5RecoveryInventory*)->bool{return false;};
 assert(!recovery_candidate_fresh(selected,failed));
 GameProgress progress{};
 assert(recovery_engine_ready(selected,progress));
 progress.queue_len=1;
 assert(!recovery_engine_ready(selected,progress));
 progress.queue_len=0;progress.state=JobState::running;
 std::strcpy(progress.title_id,"PPSA12345");
 assert(!recovery_engine_ready(selected,progress));
 progress.state=JobState::paused;
 assert(recovery_engine_ready(selected,progress));
 progress.state=JobState::error;
 assert(recovery_engine_ready(selected,progress));
 progress.state=JobState::cancelled;
 assert(recovery_engine_ready(selected,progress));
 progress.state=JobState::done;
 assert(!recovery_engine_ready(selected,progress));
 progress.state=JobState::paused;
 std::strcpy(progress.title_id,"PPSA98765");
 assert(!recovery_engine_ready(selected,progress));
 progress.state=JobState::idle;
 assert(!recovery_engine_ready(selected,progress));
 progress.title_id[0]=0;
 assert(recovery_engine_ready(selected,progress));

 /* C++/UBSan must reject malformed serialized bool bytes BEFORE
  * converting them to C++ bool; 0xff is an invalid object representation. */
 Cmp5AmprSavedRequest pinned{};
 std::memcpy(pinned.magic,"E2AREQ05",8);
 std::strcpy(pinned.source,"/mnt/usb0/homebrew/PPSA77777");
 std::strcpy(pinned.dest_base,"/mnt/usb0/homebrew");
 std::strcpy(pinned.name,"PPSA12345");
 pinned.level=9;pinned.workers=4;
 pinned.verify=true;pinned.dedup=true;
 assert(cmp5_ampr_request_valid(&pinned,"/mnt/usb0/homebrew","PPSA12345"));
 for(std::size_t field:{
   offsetof(Cmp5AmprSavedRequest,clean_leftovers),
   offsetof(Cmp5AmprSavedRequest,overwrite),
   offsetof(Cmp5AmprSavedRequest,remove_source),
   offsetof(Cmp5AmprSavedRequest,resume),
   offsetof(Cmp5AmprSavedRequest,verify),
   offsetof(Cmp5AmprSavedRequest,dedup),
   offsetof(Cmp5AmprSavedRequest,force)}){
  Cmp5AmprSavedRequest corrupt=pinned;
  reinterpret_cast<unsigned char*>(&corrupt)[field]=0xff;
  assert(!cmp5_ampr_request_valid(&corrupt,"/mnt/usb0/homebrew","PPSA12345"));
 }
 char body[256]{};
 assert(recovery_resume_body(selected,body,sizeof(body)));
 assert(!std::strcmp(body,"dest=%2Fmnt%2Fusb0%2Fhomebrew%2FPPSA12345"));
 /* A resume retains the saved level in request.bin: the pinned server
  * maps missing form 'level' to atoi("") == 0 and job_resume_level(0)
  * preserves the checkpoint value (possibly 1..12). This must not
  * silently force level 9 after closing the title. */
 assert(std::strstr(body,"level=")==nullptr);
 assert(std::strstr(body,"&")==nullptr);
 std::memset(body,'X',sizeof(body));
 assert(!recovery_resume_body(selected,body,15)&&body[0]==0);
 assert(!recovery_resume_body(selected,nullptr,sizeof(body)));

 /* A title/destination with no NUL inside its fixed field must never
  * reach strcmp/strlen. Also reject invalid stages at EVERY resume gate,
  * not just the first inventory-matching gate. */
 for(unsigned bad_stage:{0u,4u,99u}){
  Cmp5RecoveryCandidate invalid=selected;
  invalid.stage=bad_stage;
  assert(!valid_recovery_candidate(invalid));
  assert(!same_recovery_candidate(invalid,selected));
  assert(!recovery_candidate_fresh(invalid,scan));
  assert(!recovery_engine_ready(invalid,progress));
  std::memset(body,'X',sizeof(body));
  assert(!recovery_resume_body(invalid,body,sizeof(body))&&body[0]==0);
 }
 Cmp5RecoveryCandidate malformed=selected;
 std::memset(malformed.title_id,'P',sizeof(malformed.title_id));
 assert(!valid_recovery_candidate(malformed));
 assert(!same_recovery_candidate(malformed,selected));
 assert(!recovery_candidate_fresh(malformed,scan));
 assert(!recovery_engine_ready(malformed,progress));
 assert(!recovery_resume_body(malformed,body,sizeof(body))&&body[0]==0);
 malformed=selected;
 std::memset(malformed.destination,'X',sizeof(malformed.destination));
 assert(!valid_recovery_candidate(malformed));
 assert(!same_recovery_candidate(selected,malformed));
 assert(!recovery_candidate_fresh(malformed,scan));
 assert(!recovery_engine_ready(malformed,progress));
 assert(!recovery_resume_body(malformed,body,sizeof(body))&&body[0]==0);
 inventory.entries[0]=selected;
 inventory.entries[0].stage=0;
 assert(!recovery_candidate_fresh(selected,scan));
 inventory.entries[0]=selected;

 std::strcpy(selected.destination,"/user/savedata");
 assert(!recovery_resume_body(selected,body,sizeof(body))&&body[0]==0);
 assert(!recovery_candidate_fresh(selected,scan));
 puts("Post-restart checkpoint revalidation and scoped resume: PASS");
}
