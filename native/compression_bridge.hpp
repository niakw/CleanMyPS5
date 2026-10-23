// Narrow, read-only protocol check for an optional local game-compression engine.
// A successful hello is not proof of a game's compatibility or service identity.
#pragma once
#include <cstddef>
#include <cstring>
namespace cmp5_compression {
struct Hello {char version[32]{};};
inline bool parse_hello_http(const char*wire,std::size_t size,Hello*out) noexcept {
 if(!wire||!out||size<64||size>1024)return false;
 constexpr char prefix[]="HTTP/1.0 200 OK\r\n";
 if(std::memcmp(wire,prefix,sizeof(prefix)-1)!=0)return false;
 std::size_t pos=sizeof(prefix)-1,declared=0;bool type=false,length=false,ended=false;
 while(pos<size&&pos<512){
  std::size_t lineend=pos;
  while(lineend+1<size&&(wire[lineend]!='\r'||wire[lineend+1]!='\n'))lineend++;
  if(lineend+1>=size)return false;
  const char*line=wire+pos;std::size_t n=lineend-pos;
  pos=lineend+2;
  if(n==0){ended=true;break;}
  constexpr char ct[]="Content-Type: ",cl[]="Content-Length: ",te[]="Transfer-Encoding:";
  if(n>=sizeof(te)-1&&!std::memcmp(line,te,sizeof(te)-1))return false;
  if(n>=sizeof(ct)-1&&!std::memcmp(line,ct,sizeof(ct)-1)){
   if(type||n!=sizeof(ct)-1+16||
      std::memcmp(line+sizeof(ct)-1,"application/json",16))return false;
   type=true;
  }else if(n>=sizeof(cl)-1&&!std::memcmp(line,cl,sizeof(cl)-1)){
   if(length||n==sizeof(cl)-1)return false;
   length=true;
   for(std::size_t j=sizeof(cl)-1;j<n;j++){
    if(line[j]<'0'||line[j]>'9')return false;
    const unsigned d=static_cast<unsigned>(line[j]-'0');
    if(declared>(512u-d)/10u)return false;
    declared=declared*10u+d;
   }
  }
 }
 if(!type||!length||!ended||pos>size||size-pos!=declared)return false;
 constexpr char jp[]="{\"paired\":true,\"local\":true,\"version\":\"";
 constexpr char js[]="\"}";
 const auto a=sizeof(jp)-1,b=sizeof(js)-1;
 if(declared<=a+b||std::memcmp(wire+pos,jp,a)||
    std::memcmp(wire+size-b,js,b))return false;
 const auto n=declared-a-b;
 if(n>=sizeof(Hello::version))return false;
 Hello value{};
 for(std::size_t j=0;j<n;j++){
  char c=wire[pos+a+j];
  if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||
      (c>='A'&&c<='Z')||c=='.'||c=='_'||c=='-'))return false;
  value.version[j]=c;
 }
 *out=value;return true;
}
}
