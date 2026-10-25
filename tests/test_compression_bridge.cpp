#include "../native/compression_bridge.hpp"
#include <cassert>
#include <cstring>
#include <string>
static std::string reply(std::string body){
 return "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+
  std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;
}
int main(){
 using cmp5_compression::Hello;using cmp5_compression::parse_hello_http;
 const auto ok=reply(R"({"paired":true,"local":true,"version":"v1.27"})");
 Hello h{};assert(parse_hello_http(ok.data(),ok.size(),&h));
 assert(std::strcmp(h.version,"v1.27")==0);
 const auto bad=[](const std::string&s){
  Hello output{};return !parse_hello_http(s.data(),s.size(),&output);
 };
 assert(bad(reply(R"({"paired":false,"local":true,"version":"v1.27"})")));
 assert(bad(reply(R"({"paired":true,"local":false,"version":"v1.27"})")));
 assert(bad(reply(R"({"paired":true,"local":true,"version":"v1.27","admin":1})")));
 assert(bad(reply(R"({"paired":true,"local":true,"version":"v1.27","version":"v0"})")));
 assert(bad(reply(R"({"paired":true,"local":true,"version":"x\\r\\n"})")));
 assert(bad(ok.substr(0,ok.size()-1)));
 assert(bad(ok+"x"));
 auto duplicated=ok;duplicated.insert(duplicated.find("\r\n\r\n"),"\r\nContent-Length: 47");
 assert(bad(duplicated));
 auto chunked=ok;chunked.insert(chunked.find("\r\n\r\n"),"\r\nTransfer-Encoding: chunked");
 assert(bad(chunked));
 auto disguised_te=ok;disguised_te.insert(disguised_te.find("\r\n\r\n"),
                                         "\r\ntransfer-encoding: chunked");
 assert(bad(disguised_te));
 auto disguised_length=ok;
 disguised_length.insert(disguised_length.find("\r\n\r\n"),
                         "\r\nCONTENT-length: 47");
 assert(bad(disguised_length));
 auto disguised_type=ok;
 disguised_type.insert(disguised_type.find("\r\n\r\n"),
                       "\r\ncontent-type: application/json");
 assert(bad(disguised_type));
 auto malformed=ok;malformed.insert(malformed.find("\r\n\r\n"),
                                    "\r\nContent Length: 30");
 assert(bad(malformed));
 auto long_name=ok;
 long_name.insert(long_name.find("\r\n\r\n"),
                  "\r\n"+std::string(700,'X')+": untrusted");
 assert(bad(long_name));
 assert(cmp5_http::classify((std::string(3000,'Q')+": value").data(),
                            3007)==cmp5_http::Field::other);
 auto wrong=ok;wrong[7]='1';assert(bad(wrong));
 assert(!parse_hello_http(nullptr,0,&h));
 assert(!parse_hello_http(ok.data(),ok.size(),nullptr));
}
