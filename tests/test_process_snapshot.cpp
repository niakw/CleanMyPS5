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
 data.resize(start+static_cast<std::size_t>(declared),0u);
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
 for(unsigned i=0;i<max_processes+1u;i++)record(huge,int(i+1),"e",448);
 assert(decode(huge.data(),huge.size()).state==State::over_limit);
 assert(!decode(nullptr,0).safe_to_clean);
 assert(valid_user_title_id("PPSA12345"));
 assert(valid_user_title_id("CUSA00001"));
 assert(!valid_user_title_id("NPXS00001"));
 assert(!valid_user_title_id("PPSA12x45"));
 assert(!valid_user_title_id("PPSA1234"));
 auto linked=decode_with_titles(raw.data(),raw.size(),
     [](int pid,char out[10])->bool {
      const char*title=pid==144?"PPSA12345":
                       pid==211?"NPXS00001":"PPSA12345";
      std::memcpy(out,title,10);
      return true;
     });
 assert(linked.state==State::observed&&linked.identified_titles==1u);
 assert(linked.title_lookups==3u&&!linked.safe_to_clean);
 assert(std::strcmp(linked.title_ids[0],"PPSA12345")==0);
 auto linked_sample=sample_with_titles(reader,
     [](int,char out[10])->bool {
      std::memcpy(out,"CUSA12345",10);return true;
     });
 assert(linked_sample.identified_titles==1u);
 assert(std::strcmp(linked_sample.title_ids[0],"CUSA12345")==0);
 std::vector<unsigned char> many;
 for(unsigned i=0;i<max_title_lookups+1u;i++)
   record(many,int(i+1),"eboot.bin",448);
 unsigned queries=0;
 auto capped=decode_with_titles(many.data(),many.size(),
     [&](int pid,char out[10])->bool {
      ++queries;
      if(pid<6){std::memcpy(out,"PPSA00000",10);return true;}
      return false;
     });
 assert(capped.state==State::observed);
 assert(capped.title_lookups==max_title_lookups);
 assert(capped.title_lookups_limited&&queries==max_title_lookups);
 assert(capped.identified_titles==1u&&!capped.safe_to_clean);
 std::vector<unsigned char> fifth;
 for(int id=1;id<=5;id++)record(fifth,id,"eboot.bin");
 auto five=decode_with_titles(fifth.data(),fifth.size(),
     [](int pid,char out[10])->bool {
      std::snprintf(out,10,"PPSA%05d",pid);return true;
     });
 assert(five.identified_titles==displayed_titles&&five.hidden_titles==1u);
 puts("Read-only bounded process observer and fail-closed states: PASS");
}
