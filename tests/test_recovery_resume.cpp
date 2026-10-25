// Bounded and side-effect-free recovery policy tests (no PS5 access).
#include "../native/recovery_resume.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
int main(){
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
 assert(cmp5_compression::recovery_candidate_fresh(selected,scan));
 /* Another inaccessible device is not proof this selected checkpoint
  * is unsafe: the scanner only retains independently verified entries. */
 inventory.partial=1;
 assert(cmp5_compression::recovery_candidate_fresh(selected,scan));
 inventory.partial=0;inventory.hidden=1;
 assert(cmp5_compression::recovery_candidate_fresh(selected,scan));
 inventory.hidden=0;
 inventory.entries[0].stage=CMP5_RECOVERY_COPY;
 assert(!cmp5_compression::recovery_candidate_fresh(selected,scan));
 inventory.entries[0]=selected;
 std::strcpy(inventory.entries[0].title_id,"PPSA98765");
 assert(!cmp5_compression::recovery_candidate_fresh(selected,scan));
 inventory.entries[0]=selected;
 auto failed=[](const char*,Cmp5RecoveryInventory*)->bool{return false;};
 assert(!cmp5_compression::recovery_candidate_fresh(selected,failed));
 cmp5_compression::GameProgress progress{};
 assert(cmp5_compression::recovery_engine_ready(selected,progress));
 progress.queue_len=1;
 assert(!cmp5_compression::recovery_engine_ready(selected,progress));
 progress.queue_len=0;progress.state=cmp5_compression::JobState::running;
 std::strcpy(progress.title_id,"PPSA12345");
 assert(!cmp5_compression::recovery_engine_ready(selected,progress));
 progress.state=cmp5_compression::JobState::paused;
 assert(cmp5_compression::recovery_engine_ready(selected,progress));
 progress.state=cmp5_compression::JobState::error;
 assert(cmp5_compression::recovery_engine_ready(selected,progress));
 progress.state=cmp5_compression::JobState::cancelled;
 assert(cmp5_compression::recovery_engine_ready(selected,progress));
 progress.state=cmp5_compression::JobState::done;
 assert(!cmp5_compression::recovery_engine_ready(selected,progress));
 progress.state=cmp5_compression::JobState::paused;
 std::strcpy(progress.title_id,"PPSA98765");
 assert(!cmp5_compression::recovery_engine_ready(selected,progress));
 progress.state=cmp5_compression::JobState::idle;
 assert(!cmp5_compression::recovery_engine_ready(selected,progress));
 progress.title_id[0]=0;
 assert(cmp5_compression::recovery_engine_ready(selected,progress));
 char body[256]{};
 assert(cmp5_compression::recovery_resume_body(selected,body,sizeof(body)));
 assert(!std::strcmp(body,"dest=%2Fmnt%2Fusb0%2Fhomebrew%2FPPSA12345&level=9"));
 assert(!cmp5_compression::recovery_resume_body(selected,body,15));
 std::strcpy(selected.destination,"/user/savedata");
 assert(!cmp5_compression::recovery_resume_body(selected,body,sizeof(body)));
 assert(!cmp5_compression::recovery_candidate_fresh(selected,scan));
 puts("Post-restart checkpoint revalidation and scoped resume: PASS");
}
