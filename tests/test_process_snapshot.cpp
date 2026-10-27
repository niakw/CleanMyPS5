#include "../native/process_snapshot.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdio>
static void record(std::vector<unsigned char>&data,int pid,const char*name,
                   int declared=512){
 const std::size_t start=data.size();
 data.resize(start+512u,0u);
 std::memcpy(data.data()+start,&declared,sizeof(declared));
 std::memcpy(data.data()+start+72u,&pid,sizeof(pid));
 std::memcpy(data.data()+start+447u,name,std::strlen(name));
}
int main(){
 using namespace cmp5_process;
 std::vector<unsigned char> raw;
 record(raw,2,"SceShellUI");
 record(raw,144,"eboot.bin");
 record(raw,211,"payload.elf");
 auto s=decode(raw.data(),raw.size());
 assert(s.state==State::observed&&s.processes==3u);
 assert(s.eboot_names==1u&&s.elf_names==1u&&!s.safe_to_clean);
 int calls=0;
 auto reader=[&](void*dst,std::size_t*size)->bool{
  calls++;
  if(!dst){*size=raw.size();return true;}
  if(*size<raw.size())return false;
  std::memcpy(dst,raw.data(),raw.size());
  *size=raw.size();return true;
 };
 s=sample(reader);
 assert(calls==2&&s.state==State::observed&&!s.safe_to_clean);
 auto fail=[&](void*dst,std::size_t*size)->bool{
  if(!dst){*size=raw.size();return true;}
  (void)size;return false;
 };
 assert(sample(fail).state==State::unavailable);
 assert(sample([](void*,std::size_t*){return false;}).state==State::unavailable);
 auto changed=raw;
 changed.resize(raw.size()-1u);
 assert(decode(changed.data(),changed.size()).state==State::malformed);
 changed=raw;
 int negative=-1;
 std::memcpy(changed.data()+512u,&negative,sizeof(negative));
 assert(decode(changed.data(),changed.size()).state==State::malformed);
 changed=raw;
 int zero=0;
 std::memcpy(changed.data(),&zero,sizeof(zero));
 assert(decode(changed.data(),changed.size()).state==State::malformed);
 changed=raw;
 int bogus=4096;
 std::memcpy(changed.data(),&bogus,sizeof(bogus));
 assert(decode(changed.data(),changed.size()).state==State::malformed);
 auto over=[](void*,std::size_t*sz)->bool{*sz=max_bytes+1u;return true;};
 assert(sample(over).state==State::over_limit);
 std::vector<unsigned char> huge;
 for(unsigned i=0;i<max_processes+1u;i++)record(huge,int(i+1),"eboot.bin");
 assert(decode(huge.data(),huge.size()).state==State::over_limit);
 assert(!decode(nullptr,0).safe_to_clean);
 puts("Read-only bounded process observer and fail-closed states: PASS");
}
