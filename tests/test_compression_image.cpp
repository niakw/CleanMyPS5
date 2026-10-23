#include "../native/compression_image.hpp"
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
int main(){
 using cmp5_compression::ImageResolution;
 using cmp5_compression::parse_image_resolution;
 const std::string good=R"({"title_id":"PPSA12345","image_type":"exfatfs","size":22548578304})";
 ImageResolution value{};
 assert(parse_image_resolution(good.data(),good.size(),&value));
 assert(std::strcmp(value.title_id,"PPSA12345")==0);
 assert(std::strcmp(value.image_type,"exfatfs")==0);
 assert(value.bytes==22548578304ULL);
 auto bad=[](const std::string&s){ImageResolution x{};
  return !parse_image_resolution(s.data(),s.size(),&x);
 };
 auto change=[&](const char*a,const char*b){std::string copy=good;
  const auto pos=copy.find(a);assert(pos!=std::string::npos);
  copy.replace(pos,std::strlen(a),b);return copy;
 };
 assert(bad(change("PPSA12345","PPSA12../../")));
 assert(bad(change("exfatfs","pfsc")));
 assert(bad(change("exfatfs","anything")));
 assert(bad(change("22548578304","0")));
 assert(bad(change("22548578304","18446744073709551616")));
 assert(bad(change("22548578304","00022548578304")));
 assert(bad(good+"\n"));
 assert(bad(good.substr(0,good.size()-1)));
 assert(bad(change("\"size\":22548578304","\"size\":22548578304,\"extra\":1")));
 auto ufs=change("exfatfs","ufs");
 assert(parse_image_resolution(ufs.data(),ufs.size(),&value));
 auto pfs=change("exfatfs","pfs");
 assert(parse_image_resolution(pfs.data(),pfs.size(),&value));
 puts("ShadowMountPlus image title identity checks: PASS");
}
