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
 auto body=good();
 std::string wire="HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+
   std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;
 HttpJson framed{};
 assert(parse_ampr_http(wire.data(),wire.size(),&framed));
 assert(framed.size==body.size()&&framed.body&&framed.body[0]=='{');
 assert(!parse_ampr_http(wire.data(),wire.size()-1,&framed));
 auto chunked=wire;
 chunked.insert(chunked.find("\r\n\r\n"),"\r\nTransfer-Encoding: chunked");
 assert(!parse_ampr_http(chunked.data(),chunked.size(),&framed));
 auto duplicate=wire;
 duplicate.insert(duplicate.find("\r\n\r\n"),"\r\nContent-Length: 2");
 assert(!parse_ampr_http(duplicate.data(),duplicate.size(),&framed));
 auto disguised_te=wire;
 disguised_te.insert(disguised_te.find("\r\n\r\n"),
                    "\r\ntRaNsFeR-EnCoDiNg: chunked");
 assert(!parse_ampr_http(disguised_te.data(),disguised_te.size(),&framed));
 auto disguised_length=wire;
 disguised_length.insert(disguised_length.find("\r\n\r\n"),
                        "\r\ncontent-length: 1");
 assert(!parse_ampr_http(disguised_length.data(),disguised_length.size(),&framed));
 auto disguised_type=wire;
 disguised_type.insert(disguised_type.find("\r\n\r\n"),
                      "\r\nCONTENT-TYPE: text/plain");
 assert(!parse_ampr_http(disguised_type.data(),disguised_type.size(),&framed));
 auto malformed_header=wire;
 malformed_header.insert(malformed_header.find("\r\n\r\n"),
                         "\r\nBad Header: nope");
 assert(!parse_ampr_http(malformed_header.data(),malformed_header.size(),&framed));
 // Untrusted header names longer than fixed literals must never read
 // past their bounds, including in sanitizer builds.
 auto oversized_name=wire;
 oversized_name.insert(oversized_name.find("\r\n\r\n"),
                       "\r\n"+std::string(700,'X')+": opaque");
 assert(!parse_ampr_http(oversized_name.data(),oversized_name.size(),&framed));
 assert(cmp5_http::classify((std::string(3000,'Q')+": value").data(),
                            3007)==cmp5_http::Field::invalid);

 assert(pre.ready()&&pre.image_bytes==0&&pre.source_bytes==12345&&
  pre.need_max==16000&&pre.dest_total==64000&&
  std::strcmp(pre.title_id,"PPSA12345")==0);
 // Revalidation at submission must preserve exactly what was confirmed.
 // A different source length, predicted output or destination capacity is
 // not a substitute for a second affirmative controller confirmation.
 GamePreflight unchanged{};
 assert(parse_game_preflight(s.data(),s.size(),&unchanged));
 assert(same_confirmation_snapshot(pre,unchanged));
 auto snapshot_changed=[&](const char*before,const char*after){
  auto variant=good();
  const auto pos=variant.find(before);
  assert(pos!=std::string::npos);
  variant.replace(pos,std::strlen(before),after);
  GamePreflight different{};
  assert(parse_game_preflight(variant.data(),variant.size(),&different));
  assert(!same_confirmation_snapshot(pre,different));
 };
 snapshot_changed("\"title_id\":\"PPSA12345\"",
                  "\"title_id\":\"PPSA12346\"");
 snapshot_changed("\"source_bytes\":12345","\"source_bytes\":12346");
 snapshot_changed("\"image_bytes\":0","\"image_bytes\":1");
 snapshot_changed("\"need_typical\":8000","\"need_typical\":8001");
 snapshot_changed("\"need_max\":16000","\"need_max\":16001");
 snapshot_changed("\"dest_total\":64000","\"dest_total\":64001");
 snapshot_changed("\"compatible\":\"yes\"","\"compatible\":\"no\"");
 auto free_space=good();
 const auto free_pos=free_space.find("\"dest_free\":32000");
 assert(free_pos!=std::string::npos);
 free_space.replace(free_pos,std::strlen("\"dest_free\":32000"),
                    "\"dest_free\":18000");
 GamePreflight remaining{};
 assert(parse_game_preflight(free_space.data(),free_space.size(),&remaining));
 assert(same_confirmation_snapshot(pre,remaining));
 assert(!same_confirmation_snapshot(remaining,GamePreflight{}));
 auto bad=[](const std::string&s){GamePreflight x{};
  return !parse_game_preflight(s.data(),s.size(),&x);};
 auto mutate=[&](const char*a,const char*b){auto copy=good();
  auto pos=copy.find(a);assert(pos!=std::string::npos);
  copy.replace(pos,std::strlen(a),b);return copy;};
 assert(bad(mutate("\"title_id\":\"PPSA12345\"","\"title_id\":\"ABC12345\"")));
 assert(bad(mutate("\"need_max\":16000","\"need_max\":18446744073709551616")));
 assert(bad(mutate("\"need_max\":16000","\"need_max\":00016000")));
 assert(bad(mutate("\"image_bytes\":0","\"image_bytes\":18446744073709551616")));
 assert(bad(mutate("\"errors\":[]","\"errors\":[] ,\"errors\":[]")));
 assert(bad(mutate("\"warnings\":[]","\"warnings\":[] ,\"root\":true")));
 assert(bad(mutate("\"compatible\":\"yes\"","\"compatible\":\"maybe\"")));
 assert(bad(mutate("\"duration_by_level\":[1,2,3,4,5,6,7,8,9,10,11,12]",
    "\"duration_by_level\":[1,2]")));
 assert(bad(s.substr(0,s.size()-1)));
 assert(bad(s+"\n"));
 assert(bad(mutate("\"title\":\"A \\\"game\\\"\"","\"title\":\"A \n game\"")));
 s=mutate("\"compatible\":\"yes\"","\"compatible\":\"no\"");
 assert(parse_game_preflight(s.data(),s.size(),&pre)&&!pre.ready()&&!pre.compatibility_pending);
 s=mutate("\"compatible\":\"yes\"","\"compatible\":\"later\"");
 assert(parse_game_preflight(s.data(),s.size(),&pre));
 assert(pre.compatibility_pending&&!pre.compatible&&!pre.ready());
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
 StartAck acknowledgment{};
 assert(parse_start_ack(R"({"ok":true,"position":0})",24,&acknowledgment));
 assert(acknowledgment.position==0);
 assert(parse_start_ack(R"({"ok":true,"position":15})",25,&acknowledgment));
 assert(acknowledgment.position==15);
 assert(!parse_start_ack(R"({"ok":true,"position":16})",25,&acknowledgment));
 assert(!parse_start_ack(R"({"ok":false,"position":0})",25,&acknowledgment));
 assert(!parse_start_ack(R"({"ok":true,"position":0,"remove":true})",38,&acknowledgment));
 assert(!parse_start_ack(R"({"ok":true,"position":0})",24,nullptr));
 puts("AMPR game-specific preflight and path validation: PASS");
}
