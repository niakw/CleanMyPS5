// Strict, bounded decoder for the PINNED embedded AMPR check protocol.
// Fail closed if the engine changes its response shape or fields.
// Pure C++20: host tests and PS5 app use exactly the same parser.
// Copyright (C) 2026 CleanMyPS5 contributors. GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace cmp5_compression {

struct HttpJson {
 const char*body=nullptr;
 std::size_t size=0;
};
// Embedded upstream uses HTTP/1.0, strict Content-Length, no chunked.
inline bool parse_ampr_http(const char*wire,std::size_t n,
                            HttpJson*out) noexcept {
 if(out)*out={};
 if(!wire||!out||n<60||n>8192)return false;
 constexpr char status[]="HTTP/1.0 200 OK\r\n";
 if(n<sizeof(status)-1||std::memcmp(wire,status,sizeof(status)-1))return false;
 std::size_t p=sizeof(status)-1,length=0;
 bool ct=false,cl=false,done=false;
 while(p<n&&p<1024){
  std::size_t end=p;
  while(end+1<n&&(wire[end]!='\r'||wire[end+1]!='\n'))++end;
  if(end+1>=n)return false;
  const char*line=wire+p;const std::size_t size=end-p;
  p=end+2;
  if(size==0){done=true;break;}
  constexpr char type[]="Content-Type: ",len[]="Content-Length: ",
                 enc[]="Transfer-Encoding:";
  if(size>=sizeof(enc)-1&&!std::memcmp(line,enc,sizeof(enc)-1))
   return false;
  if(size>=sizeof(type)-1&&!std::memcmp(line,type,sizeof(type)-1)){
   if(ct||size!=sizeof(type)-1+16||
      std::memcmp(line+sizeof(type)-1,"application/json",16))return false;
   ct=true;
  }
  if(size>=sizeof(len)-1&&!std::memcmp(line,len,sizeof(len)-1)){
   if(cl||size==sizeof(len)-1)return false;
   cl=true;
   for(std::size_t i=sizeof(len)-1;i<size;i++){
    if(line[i]<'0'||line[i]>'9')return false;
    const unsigned digit=static_cast<unsigned>(line[i]-'0');
    if(length>(4096u-digit)/10u)return false;
    length=length*10u+digit;
   }
  }
 }
 if(!done||!ct||!cl||!length||p>=n||length!=n-p||
    wire[p]!='{')return false;
 *out={wire+p,length};
 return true;
}
struct GamePreflight {
 char title_id[16]{};
 std::uint64_t source_bytes=0,image_bytes=0,need_typical=0,need_max=0,dest_free=0,dest_total=0;
 unsigned errors=0,warnings=0;
 bool compatible=false,compatibility_pending=false;
 bool ready() const noexcept {
  return compatible&&title_id[0]&&source_bytes>0&&need_max>0&&
    dest_free>=need_max&&errors==0&&warnings==0;
 }
};
class PreflightParser {
 const char*p;const char*end;
 bool take(char x) noexcept {if(p==end||*p!=x)return false;++p;return true;}
 bool literal(const char*v) noexcept {
  const auto n=std::strlen(v);
  if(static_cast<std::size_t>(end-p)<n||std::memcmp(p,v,n))return false;
  p+=n;return true;
 }
 bool field(const char*k) noexcept {
  return take('"')&&literal(k)&&take('"')&&take(':');
 }
 bool string(char*out,std::size_t cap) noexcept {
  if(!take('"'))return false;
  std::size_t used=0;
  while(p<end){
   unsigned char c=static_cast<unsigned char>(*p++);
   if(c=='"'){if(out){if(used>=cap)return false;out[used]=0;}return true;}
   if(c<0x20)return false;
   if(c=='\\'){
    if(p==end)return false;
    c=static_cast<unsigned char>(*p++);
    if(c=='u'){
     for(unsigned i=0;i<4;i++){
      if(p==end)return false;
      const char d=*p++;
      if(!((d>='0'&&d<='9')||(d>='a'&&d<='f')||
           (d>='A'&&d<='F')))return false;
     }
     if(out)return false; // no decoded title IDs / variants
     continue;
    }
    if(c!='"'&&c!='\\'&&c!='/'&&c!='b'&&c!='f'&&
       c!='n'&&c!='r'&&c!='t')return false;
    if(out)return false;
    continue;
   }
   if(out){if(used+1>=cap)return false;out[used++]=static_cast<char>(c);}
  }
  return false;
 }
 bool positive(std::uint64_t*out) noexcept {
  if(p==end||*p<'0'||*p>'9')return false;
  if(*p=='0'){++p;if(p<end&&*p>='0'&&*p<='9')return false;
   *out=0;return true;}
  std::uint64_t v=0;
  do{
   const unsigned d=static_cast<unsigned>(*p-'0');
   if(v>(UINT64_MAX-d)/10u)return false;
   v=v*10u+d;++p;
  }while(p<end&&*p>='0'&&*p<='9');
  *out=v;return true;
 }
 bool integer() noexcept {
  if(p<end&&*p=='-')++p;
  std::uint64_t n=0;return positive(&n);
 }
 bool string_list(unsigned*out) noexcept {
  if(!take('['))return false;
  unsigned n=0;
  if(take(']')){*out=0;return true;}
  for(;;){
   if(++n>20||!string(nullptr,0))return false;
   if(take(']'))break;
   if(!take(','))return false;
  }
  *out=n;return true;
 }
 bool duration_list() noexcept {
  if(!take('['))return false;
  unsigned n=0;
  for(;;){
   if(n>=12||!integer())return false;
   ++n;
   if(take(']'))return n==12;
   if(!take(','))return false;
  }
 }
 bool compatibility(bool*out,bool*pending) noexcept {
  char v[16]{};
  if(!string(v,sizeof(v)))return false;
  if(std::strcmp(v,"yes")==0)*out=true;
  else if(std::strcmp(v,"no")==0||std::strcmp(v,"unknown")==0)
   *out=false;
  else if(std::strcmp(v,"later")==0){
   *out=false;*pending=true; /* no mounted source; NEVER auto-approve */
  }
  else return false;
  return true;
 }
public:
 PreflightParser(const char*data,std::size_t n):p(data),end(data+n){}
 bool parse(GamePreflight*out) noexcept {
  if(!out||!take('{')||!field("title_id")||
     !string(out->title_id,sizeof(out->title_id))||
     !take(',')||!field("title")||!string(nullptr,0)||
     !take(',')||!field("source_bytes")||!positive(&out->source_bytes)||
     !take(',')||!field("image_bytes"))return false;
  if(!positive(&out->image_bytes)||!take(',')||!field("estimate")||
     !string(nullptr,0)||!take(',')||!field("need_typical")||
     !positive(&out->need_typical)||!take(',')||!field("need_max")||
     !positive(&out->need_max)||!take(',')||!field("duration_by_level")||
     !duration_list()||!take(',')||!field("verify_seconds")||
     !integer()||!take(',')||!field("dest_free")||
     !positive(&out->dest_free)||!take(',')||!field("dest_total")||
     !positive(&out->dest_total)||!take(',')||!field("compatible")||
     !compatibility(&out->compatible,&out->compatibility_pending)||!take(',')||
     !field("errors")||!string_list(&out->errors)||!take(',')||
     !field("warnings")||!string_list(&out->warnings)||
     !take('}')||p!=end)return false;
  // An actual title id: four uppercase letters, five decimal digits.
  if(std::strlen(out->title_id)!=9)return false;
  for(unsigned i=0;i<9;i++){
   char c=out->title_id[i];
   if(i<4?(c<'A'||c>'Z'):(c<'0'||c>'9'))return false;
  }
  return out->dest_free<=out->dest_total&&out->need_typical<=out->need_max;
 }
};
inline bool parse_game_preflight(const char*data,std::size_t n,
                                 GamePreflight*out) noexcept {
 if(!data||!out||n<180||n>4096)return false;
 GamePreflight value{};
 if(!PreflightParser(data,n).parse(&value))return false;
 *out=value;return true;
}

/* The user confirms one particular conversion plan, not merely a title ID.
 * A second check is mandatory just before POST /api/start. Space can
 * decrease or increase between checks; provided the new plan remains ready,
 * it must not silently substitute a different source, estimated output or
 * destination volume capacity for the one the user confirmed.
 *
 * This is a freshness/consent check, not proof of mount identity. The engine
 * must still revalidate source inodes/permissions and output safety.
 */
inline bool same_confirmation_snapshot(const GamePreflight&confirmed,
                                      const GamePreflight&current) noexcept {
 return confirmed.ready()&&current.ready()&&
  std::strcmp(confirmed.title_id,current.title_id)==0&&
  confirmed.source_bytes==current.source_bytes&&
  confirmed.image_bytes==current.image_bytes&&
  confirmed.need_typical==current.need_typical&&
  confirmed.need_max==current.need_max&&
  confirmed.dest_total==current.dest_total;
}

struct StartAck {unsigned position=0;};
inline bool parse_start_ack(const char*json,std::size_t size,
                            StartAck*out) noexcept {
 // Upstream's 200 response is {"ok":true,"position":N}, N in 0..15.
 // Exact framing prevents a crafted unrelated JSON nested field from
 // masquerading as a successfully submitted conversion.
 if(!json||!out||size<24||size>48)return false;
 constexpr char start[]="{\"ok\":true,\"position\":";
 constexpr auto prefix=sizeof(start)-1;
 if(size<=prefix+1||std::memcmp(json,start,prefix)!=0||
    json[size-1]!='}')return false;
 unsigned pos=0;
 for(std::size_t i=prefix;i<size-1;i++){
  const char c=json[i];
  if(c<'0'||c>'9'||(i==prefix&&c=='0'&&i+1!=size-1))
   return false;
  pos=pos*10u+static_cast<unsigned>(c-'0');
  if(pos>=16u)return false;
 }
 *out={pos};return true;
}
/* Form component used solely with the bounded candidates returned by
 * cmp5_scan_game_images. Reject controls / traversal before encoding. */
inline bool form_encode_path(const char*path,char*out,std::size_t cap) noexcept {
 if(!path||!out||path[0]!='/'||cap<16)return false;
 constexpr char digits[]="0123456789ABCDEF";
 std::size_t j=0,n=0;
 const char*component=path+1;
 for(const unsigned char*p=(const unsigned char*)path;*p;p++){
  if(++n>191u)return false;
  const char c=static_cast<char>(*p);
  if(c=='?'||c=='#'||c=='%'||c=='&'||c=='='||c=='\\'||
    c=='\r'||c=='\n'||*p<0x20||*p>=0x7f)return false;
  if(c=='/'){
   if(p!=(const unsigned char*)path){
    if(p==(const unsigned char*)component||!std::strncmp(component,"..",2)||
       (p-(const unsigned char*)component==1u&&component[0]=='.'))return false;
    component=(const char*)p+1;
   }
  }
  const bool plain=(*p>='0'&&*p<='9')||(*p>='a'&&*p<='z')||
    (*p>='A'&&*p<='Z')||c=='-'||c=='_'||c=='.';
  const std::size_t add=plain?1u:3u;
  if(j+add>=cap)return false;
  if(plain)out[j++]=c;
  else {out[j++]='%';out[j++]=digits[*p>>4];out[j++]=digits[*p&15];}
 }
 if(path[n-1]=='/'||!std::strcmp(component,"..")||
    !std::strcmp(component,"."))return false;
 out[j]=0;return true;
}
}
