#include "../native/compression_preflight.hpp"
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
static std::string good(){
 return R"({"title_id":"PPSA12345","title":"A \"game\"","source_bytes":12345,"image_bytes":0,"estimate":"sample","need_typical":8000,"need_max":16000,"duration_by_level":[1,2,3,4,5,6,7,8,9,10,11,12],"verify_seconds":-1,"dest_free":32000,"dest_total":64000,"compatible":"yes","errors":[],"warnings":[]})";
}
int main(){
 using namespace cmp5_compression;
 GamePreflight pre{};
 auto s=good();assert(parse_game_preflight(s.data(),s.size(),&pre));
 assert(pre.ready()&&pre.source_bytes==12345&&
  pre.need_max==16000&&std::strcmp(pre.title_id,"PPSA12345")==0);
 auto bad=[](const std::string&s){GamePreflight x{};
  return !parse_game_preflight(s.data(),s.size(),&x);};
 auto mutate=[&](const char*a,const char*b){auto copy=good();
  auto pos=copy.find(a);assert(pos!=std::string::npos);
  copy.replace(pos,std::strlen(a),b);return copy;};
 assert(bad(mutate("\"title_id\":\"PPSA12345\"","\"title_id\":\"ABC12345\"")));
 assert(bad(mutate("\"need_max\":16000","\"need_max\":18446744073709551616")));
 assert(bad(mutate("\"need_max\":16000","\"need_max\":00016000")));
 assert(bad(mutate("\"errors\":[]","\"errors\":[] ,\"errors\":[]")));
 assert(bad(mutate("\"warnings\":[]","\"warnings\":[] ,\"root\":true")));
 assert(bad(mutate("\"compatible\":\"yes\"","\"compatible\":\"maybe\"")));
 assert(bad(mutate("\"duration_by_level\":[1,2,3,4,5,6,7,8,9,10,11,12]",
    "\"duration_by_level\":[1,2]")));
 assert(bad(s.substr(0,s.size()-1)));
 assert(bad(s+"\n"));
 assert(bad(mutate("\"title\":\"A \\\"game\\\"\"","\"title\":\"A \n game\"")));
 s=mutate("\"compatible\":\"yes\"","\"compatible\":\"no\"");
 assert(parse_game_preflight(s.data(),s.size(),&pre)&&!pre.ready());
 s=mutate("\"errors\":[]","\"errors\":[\"not_ampr\"]");
 assert(parse_game_preflight(s.data(),s.size(),&pre)&&pre.errors==1&&!pre.ready());
 s=mutate("\"warnings\":[]","\"warnings\":[\"space_low\"]");
 assert(parse_game_preflight(s.data(),s.size(),&pre)&&pre.warnings==1&&!pre.ready());
 s=mutate("\"dest_free\":32000","\"dest_free\":1000");
 assert(parse_game_preflight(s.data(),s.size(),&pre)&&!pre.ready());
 char encoded[600]{};
 assert(form_encode_path("/mnt/usb0/homebrew/PPSA12345",encoded,sizeof(encoded)));
 assert(std::strcmp(encoded,"%2Fmnt%2Fusb0%2Fhomebrew%2FPPSA12345")==0);
 assert(!form_encode_path("/data/../user/saved",encoded,sizeof(encoded)));
 assert(!form_encode_path("/data/abc%2Fdef",encoded,sizeof(encoded)));
 assert(!form_encode_path("/data/x\nPOST /api/start",encoded,sizeof(encoded)));
 assert(!form_encode_path("/data/homebrew/",encoded,sizeof(encoded)));
 assert(!form_encode_path("/data//PPSA",encoded,sizeof(encoded)));
 puts("AMPR game-specific preflight and path validation: PASS");
}
