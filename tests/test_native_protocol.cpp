// Regression tests for the PS5 title's strict read-only loopback decoder.
#include "native/loopback_protocol.hpp"
#include "native/privilege_contract.hpp"
#include <cassert>
#include <string>

namespace {
std::string response(const std::string&body,
                     const std::string&type="application/json; charset=utf-8"){
 return "HTTP/1.1 200 OK\r\nContent-Type: "+type+
   "\r\nContent-Length: "+std::to_string(body.size())+
   "\r\nConnection: close\r\n\r\n"+body;
}
bool accepts(const std::string&http){
 cmp5_native::HttpBody body{};
 return cmp5_native::parse_http_json(http.data(),http.size(),&body)&&
   body.data>=http.data() && body.size<=http.size() &&
   body.data+body.size==http.data()+http.size();
}
}
int main(){
 using cmp5_privilege::Actor;
 using cmp5_privilege::Operation;
 using cmp5_privilege::allows;
 assert(allows(Actor::native_title,Operation::inspect_metadata));
 assert(allows(Actor::diagnostic_helper,Operation::inspect_metadata));
 assert(!allows(Actor::compression_engine,Operation::inspect_metadata));
 assert(allows(Actor::native_title,Operation::request_compression));
 assert(!allows(Actor::diagnostic_helper,Operation::request_compression));
 assert(allows(Actor::compression_engine,Operation::execute_compression));
 assert(!allows(Actor::native_title,Operation::execute_compression));
 for(auto actor:{Actor::native_title,Actor::diagnostic_helper,Actor::compression_engine}){
  assert(!cmp5_privilege::can_delete(actor));
  for(auto op:{Operation::delete_gallery_media,Operation::clean_system_cache,
               Operation::uninstall_title,Operation::change_autoload,
               Operation::restore_gallery,Operation::elevate_credentials})
   assert(!allows(actor,op));
 }
 const std::string json="{\"schemaVersion\":1,\"mode\":\"read-only\"}";
 const auto good=response(json);
 cmp5_native::HttpBody body{};
 assert(cmp5_native::parse_http_json(good.data(),good.size(),&body));
 assert(std::string(body.data,body.size)==json);
 assert(accepts(good));
 // Match exact expected read-only helper contract; a web server or an older
 // helper reusing :8877 cannot silently become a source of native scan data.
 constexpr auto*cap=cmp5_native::expected_capabilities;
 assert(cmp5_native::matches_expected_helper(cap,std::strlen(cap)));
 assert(!cmp5_native::matches_expected_helper(cap,std::strlen(cap)-1));
 std::string stale(cap);
 stale.replace(stale.find("\"protocol\":2"),std::strlen("\"protocol\":2"),"\"protocol\":1");
 assert(!cmp5_native::matches_expected_helper(stale.data(),stale.size()));
 std::string writable(cap);
 writable.replace(writable.find("\"delete\":false"),std::strlen("\"delete\":false"),
                  "\"delete\":true");
 assert(!cmp5_native::matches_expected_helper(writable.data(),writable.size()));
 assert(!cmp5_native::matches_expected_helper(nullptr,0));
 assert(!accepts("HTTP/1.1 503 Service Unavailable\r\n"
                 "Content-Type: application/json\r\nContent-Length: 2\r\n\r\n{}"));
 assert(!accepts("HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/html\r\nContent-Length: 2\r\n\r\n{}"));
 assert(!accepts("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}"));
 assert(!accepts("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{}"));
 assert(!accepts("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                 "Content-Length: 9999999999999999999\r\n\r\n{}"));
 assert(!accepts("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                 "Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}"));
 assert(!accepts("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                 "Content-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\n{}"));
 assert(!accepts("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                 "Content-Length: 2\r\n\r\n[]"));
 assert(!accepts(good+"trailing"));
 assert(!accepts(good.substr(0,good.size()-1)));
 assert(!accepts(good.substr(0,good.find("\r\n\r\n")+2)));
 for(std::size_t size=0;size<good.size();size++){
  assert(!cmp5_native::parse_http_json(good.data(),size,&body));
 }
 std::string padded(2050,'X');
 assert(!accepts("HTTP/1.1 200 OK\r\n"+padded+"\r\nContent-Type: application/json"
                 "\r\nContent-Length: 2\r\n\r\n{}"));
 return 0;
}
