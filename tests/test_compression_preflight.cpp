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
 assert(std::strcmp(pre.first_error,"not_ampr")==0);
 assert(std::strcmp(preflight_issue_label(pre),"JEU INCOMPATIBLE AVEC AMPR")==0);
 s=mutate("\"warnings\":[]","\"warnings\":[\"space_low\"]");
 assert(parse_game_preflight(s.data(),s.size(),&pre)&&pre.warnings==1&&!pre.ready());
 assert(std::strcmp(pre.first_warning,"space_low")==0);
 assert(std::strcmp(preflight_issue_label(pre),"ESPACE DESTINATION TROP FAIBLE")==0);
 s=mutate("\"errors\":[]",
   "\"errors\":[\"game_running\",\"source_missing\"]");
 assert(parse_game_preflight(s.data(),s.size(),&pre)&&pre.errors==2&&!pre.ready());
 assert(std::strcmp(pre.first_error,"game_running")==0);
 assert(std::strcmp(preflight_issue_label(pre),"FERMER LE JEU AVANT CONVERSION")==0);
 s=mutate("\"errors\":[]","\"errors\":[\"/user/secret/path\"]");
 assert(parse_game_preflight(s.data(),s.size(),&pre)&&pre.errors==1);
 assert(pre.first_error[0]==0);
 assert(std::strcmp(preflight_issue_label(pre),"BLOCAGE SIGNALE PAR LE MOTEUR")==0);
 s=mutate("\"errors\":[]","\"errors\":[\"unknown_future_code\"]");
 assert(parse_game_preflight(s.data(),s.size(),&pre)&&pre.errors==1);
 assert(std::strcmp(preflight_issue_label(pre),"BLOCAGE SIGNALE PAR LE MOTEUR")==0);
 /* Genuine worker /api/check for an absent or non-game folder can return
  * title_id:"". These diagnostic-only frames must parse but cannot arm
  * the conversion confirmation because ready() requires a real ID. */
 auto no_title=good();
 auto change_no_title=[&](const char*before,const char*after){
  const auto pos=no_title.find(before);assert(pos!=std::string::npos);
  no_title.replace(pos,std::strlen(before),after);
 };
 change_no_title("\"title_id\":\"PPSA12345\"","\"title_id\":\"\"");
 change_no_title("\"compatible\":\"yes\"","\"compatible\":\"later\"");
 change_no_title("\"errors\":[]","\"errors\":[\"source_missing\"]");
 assert(parse_game_preflight(no_title.data(),no_title.size(),&pre));
 assert(pre.title_id[0]==0&&!pre.ready()&&pre.errors==1);
 assert(std::strcmp(preflight_issue_label(pre),"SOURCE DU JEU INTROUVABLE")==0);
 auto bad_no_title=no_title;
 auto no_title_mutate=[&](const char*before,const char*after){
  const auto pos=bad_no_title.find(before);assert(pos!=std::string::npos);
  bad_no_title.replace(pos,std::strlen(before),after);
 };
 no_title_mutate("source_missing","game_running");
 assert(bad(bad_no_title)); /* Never allow unrelated no-ID errors. */
 bad_no_title=no_title;
 auto pos=bad_no_title.find("\"compatible\":\"later\"");assert(pos!=std::string::npos);
 bad_no_title.replace(pos,std::strlen("\"compatible\":\"later\""),"\"compatible\":\"yes\"");
 assert(bad(bad_no_title));
 bad_no_title=no_title;
 pos=bad_no_title.find("\"errors\":[\"source_missing\"]");assert(pos!=std::string::npos);
 bad_no_title.replace(pos,std::strlen("\"errors\":[\"source_missing\"]"),"\"errors\":[]");
 assert(bad(bad_no_title));
 auto non_game=no_title;
 pos=non_game.find("source_missing");assert(pos!=std::string::npos);
 non_game.replace(pos,std::strlen("source_missing"),"not_game");
 assert(parse_game_preflight(non_game.data(),non_game.size(),&pre));
 assert(!pre.ready());
 assert(std::strcmp(preflight_issue_label(pre),"DOSSIER NON RECONNU COMME JEU")==0);
 auto small=display_gib(1,true);
 assert(small.whole==0&&small.hundredths==1);
 small=display_gib(268435456ULL,true);
 assert(small.whole==0&&small.hundredths==25);
 small=display_gib(1073741824ULL-1,true);
 assert(small.whole==1&&small.hundredths==0);
 small=display_gib(1073741824ULL-1,false);
 assert(small.whole==0&&small.hundredths==99);
 small=display_gib(1073741824ULL+1,false);
 assert(small.whole==1&&small.hundredths==0);
 small=display_gib(0,true);
 assert(small.whole==0&&small.hundredths==0);
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
 char pause[256]{};
 /* Pause and resume may only address the agreed game output roots.
  * In particular, the controller itself must reject user/system paths. */
 assert(allowed_destination("/data/homebrew"));
 assert(allowed_destination("/mnt/usb0/homebrew"));
 assert(allowed_destination("/mnt/ext7/homebrew"));
 assert(!allowed_destination(nullptr));
 assert(!allowed_destination("/mnt/usb8/homebrew"));
 assert(!allowed_destination("/mnt/usb0/homebrew/../savedata"));
 assert(!allowed_destination("/mnt/usb0/homebrew/"));
 assert(!allowed_destination("/user/savedata"));
 assert(!allowed_destination("/data/homebrew/../user"));
 assert(!build_pause_body("/user/savedata","PPSA12345",pause,sizeof(pause)));
 assert(!build_pause_body("/mnt/usb8/homebrew","PPSA12345",pause,sizeof(pause)));
 assert(build_pause_body("/mnt/usb0/homebrew","PPSA12345",pause,sizeof(pause)));
 assert(std::strcmp(pause,
   "dest=%2Fmnt%2Fusb0%2Fhomebrew%2FPPSA12345")==0);
 assert(build_pause_body("/data/homebrew","CUSA12345",pause,sizeof(pause)));
 assert(std::strcmp(pause,"dest=%2Fdata%2Fhomebrew%2FCUSA12345")==0);
 assert(!build_pause_body("/data/homebrew","PPSA1234A",pause,sizeof(pause)));
 assert(!build_pause_body("/data/homebrew","INVALID",pause,sizeof(pause)));
 assert(!build_pause_body("/data/../homebrew","PPSA12345",pause,sizeof(pause)));
 assert(!build_pause_body("/data/homebrew/","PPSA12345",pause,sizeof(pause)));
 assert(!build_pause_body("/data/homebrew","PPSA12345",pause,12));
 assert(!build_pause_body("/data/homebrew","PPSA12345",nullptr,256));
 /* Deterministic POST transport: no real socket or engine. A failure
  * before the first BODY byte is certain non-delivery; after any
  * positive write it is ambiguous even if the remainder is cancelled. */
 const char payload[]="dest=%2Fdata%2Fhomebrew%2FPPSA12345";
 bool body_sent=true;
 unsigned sent_calls=0;
 assert(!send_mutation_body(payload,sizeof(payload)-1u,
   [&](const char*,std::size_t)->int{++sent_calls;return 1;},
   []{return true;},&body_sent));
 assert(!body_sent&&sent_calls==0u);
 assert(!send_mutation_body(payload,sizeof(payload)-1u,
   [&](const char*,std::size_t)->int{++sent_calls;return -1;},
   []{return false;},&body_sent));
 assert(!body_sent&&sent_calls==1u);
 sent_calls=0;
 assert(!send_mutation_body(payload,sizeof(payload)-1u,
   [&](const char*,std::size_t n)->int{
    ++sent_calls;return static_cast<int>(n+1u);
   },[]{return false;},&body_sent));
 assert(!body_sent&&sent_calls==1u);
 sent_calls=0;
 assert(!send_mutation_body(payload,sizeof(payload)-1u,
   [&](const char*,std::size_t n)->int{
    ++sent_calls;return static_cast<int>(n>3u?3u:n);
   },[&]{return sent_calls>0u;},&body_sent));
 assert(body_sent&&sent_calls==1u);
 std::string assembled{};
 sent_calls=0;
 assert(send_mutation_body(payload,sizeof(payload)-1u,
   [&](const char*data,std::size_t n)->int{
    const auto take=n>4u?4u:n;
    assembled.append(data,take);++sent_calls;
    return static_cast<int>(take);
   },[]{return false;},&body_sent));
 assert(body_sent&&assembled==payload&&sent_calls>1u);
 assert(!send_mutation_body(payload,0,
   [](const char*,std::size_t)->int{return 1;},
   []{return false;},&body_sent)&&!body_sent);
 assert(!send_mutation_body(nullptr,3,
   [](const char*,std::size_t)->int{return 1;},
   []{return false;},&body_sent)&&!body_sent);
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
