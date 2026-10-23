#include "../native/compression_progress.hpp"
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
int main(){
 using namespace cmp5_compression;
 ProgressRefresh refresh{};
 assert(!refresh.armed()&&!refresh.due(1000));
 refresh.defer(1000);
 assert(refresh.armed());
 assert(!refresh.due(1000+ProgressRefresh::interval_ns-1));
 assert(refresh.due(1000+ProgressRefresh::interval_ns));
 refresh.defer(1000+ProgressRefresh::interval_ns);
 assert(!refresh.due(1000+ProgressRefresh::interval_ns));
 refresh.clear();
 assert(!refresh.armed()&&!refresh.due(UINT64_MAX));
 refresh.defer(UINT64_MAX);
 assert(!refresh.armed()); // Reject impossible monotonic timestamp overflow.
 const std::string sample=R"({"service":"CleanMyPS5-AMPR","state":"running","percent":45,"bytes_done":450,"bytes_total":1000,"queue_len":1,"launch_checked":false,"title_id":"PPSA12345"})";
 GameProgress p{};
 assert(parse_progress(sample.data(),sample.size(),&p));
 assert(p.state==JobState::running&&p.percent==45&&p.bytes_done==450&&
   p.bytes_total==1000&&p.queue_len==1&&!p.launch_checked&&std::strcmp(p.title_id,"PPSA12345")==0);
 const auto bad=[](const std::string&s){
  GameProgress p{};return !parse_progress(s.data(),s.size(),&p);
 };
 auto mutate=[&](const char*a,const char*b){
  std::string result=sample;
  auto pos=result.find(a);assert(pos!=std::string::npos);
  result.replace(pos,std::strlen(a),b);return result;
 };
 assert(bad(mutate("\"percent\":45","\"percent\":101")));
 assert(bad(mutate("\"state\":\"running\"","\"state\":\"misleading\"")));
 assert(bad(mutate("\"queue_len\":1","\"queue_len\":17")));
 assert(bad(mutate("\"bytes_done\":450","\"bytes_done\":1001")));
 assert(bad(mutate("\"bytes_total\":1000","\"bytes_total\":18446744073709551616")));
 assert(bad(mutate("\"state\":\"running\"","\"state\":\"running\",\"state\":\"done\"")));
 assert(bad(mutate("\"title_id\":\"PPSA12345\"",
                   "\"title_id\":\"invalid\"")));
 assert(bad(mutate("\"launch_checked\":false","\"launch_checked\":true,\"secret\":12")));
 assert(bad(sample.substr(0,sample.size()-1)));
 assert(bad(sample+"\n"));
 std::string done=mutate("\"state\":\"running\"","\"state\":\"done\"");
 assert(parse_progress(done.data(),done.size(),&p)&&p.state==JobState::done);
 assert(!p.launch_checked); // finished packing != launch proof
 puts("CleanMyPS5 embedded AMPR progress parser: PASS");
}
