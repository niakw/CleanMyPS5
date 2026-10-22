// Allocation-free, bounded JSON decoder for the native read-only overview.
// Do not confuse similarly named keys in nested objects with root counters.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cmp5_native {
struct Overview {
 unsigned files=0,media=0,cache=0,logs=0;
 unsigned large=0,old=0,old_large=0;
 std::uint64_t bytes=0,large_bytes=0;
 bool partial=false;
};
class OverviewReader {
 const char*at;
 const char*end;
 enum class Scope { root,categories,review,ignored };
 struct Span { const char*data=nullptr;std::size_t size=0;bool escaped=false; };
 static bool key(Span s,const char*want) noexcept {
  const std::size_t n=std::strlen(want);
  return !s.escaped&&s.size==n&&std::memcmp(s.data,want,n)==0;
 }
 void whitespace() noexcept {
  while(at<end&&(*at==' '||*at=='\n'||*at=='\r'||*at=='\t'))++at;
 }
 bool take(char c) noexcept {
  whitespace();if(at==end||*at!=c)return false;
  ++at;return true;
 }
 bool literal(const char*s) noexcept {
  whitespace();
  const std::size_t n=std::strlen(s);
  if(static_cast<std::size_t>(end-at)<n||std::memcmp(at,s,n))return false;
  at+=n;return true;
 }
 static bool hex(char c) noexcept {
  return (c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f');
 }
 bool str(Span*out) noexcept {
  whitespace();
  if(at==end||*at!='"')return false;
  ++at;const char*start=at;bool escaped=false;
  while(at<end){
   const unsigned char c=static_cast<unsigned char>(*at++);
   if(c=='"'){
    if(out)*out={start,static_cast<std::size_t>((at-1)-start),escaped};
    return true;
   }
   if(c<0x20)return false;
   if(c!='\\')continue;
   escaped=true;
   if(at==end)return false;
   const char e=*at++;
   if(e=='u'){
    for(int i=0;i<4;i++){if(at==end||!hex(*at++))return false;}
   }else if(e!='"'&&e!='\\'&&e!='/'&&e!='b'&&e!='f'&&
            e!='n'&&e!='r'&&e!='t')return false;
  }
  return false;
 }
 bool unsigned_number(std::uint64_t*out) noexcept {
  whitespace();
  if(at==end||*at<'0'||*at>'9')return false;
  if(*at=='0'){
   ++at;
   if(at!=end&&*at>='0'&&*at<='9')return false;
   *out=0;return true;
  }
  std::uint64_t value=0;
  while(at!=end&&*at>='0'&&*at<='9'){
   const unsigned d=static_cast<unsigned>(*at-'0');
   if(value>(UINT64_MAX-d)/10u)return false;
   value=value*10u+d;++at;
  }
  *out=value;return true;
 }
 bool uint_field(unsigned*out) noexcept {
  std::uint64_t n=0;
  if(!unsigned_number(&n)||n>UINT32_MAX)return false;
  *out=static_cast<unsigned>(n);return true;
 }
 bool number_ignored() noexcept {
  whitespace();
  if(at!=end&&*at=='-')++at;
  if(at==end)return false;
  if(*at=='0')++at;
  else {
   if(*at<'1'||*at>'9')return false;
   while(at!=end&&*at>='0'&&*at<='9')++at;
  }
  if(at!=end&&*at=='.'){
   ++at;
   if(at==end||*at<'0'||*at>'9')return false;
   while(at!=end&&*at>='0'&&*at<='9')++at;
  }
  if(at!=end&&(*at=='e'||*at=='E')){
   ++at;if(at!=end&&(*at=='+'||*at=='-'))++at;
   if(at==end||*at<'0'||*at>'9')return false;
   while(at!=end&&*at>='0'&&*at<='9')++at;
  }
  return true;
 }
 bool ignore(unsigned depth) noexcept {
  whitespace();
  if(depth>12||at==end)return false;
  if(*at=='{')return object(Scope::ignored,nullptr,depth+1);
  if(*at=='['){
   ++at;whitespace();
   if(take(']'))return true;
   for(;;){
    if(!ignore(depth+1))return false;
    if(take(']'))return true;
    if(!take(','))return false;
   }
  }
  if(*at=='"')return str(nullptr);
  if(*at=='t')return literal("true");
  if(*at=='f')return literal("false");
  if(*at=='n')return literal("null");
  return number_ignored();
 }
 bool object(Scope scope,Overview*out,unsigned depth) noexcept {
  if(depth>12||!take('{'))return false;
  unsigned mask=0;
  if(take('}'))return scope==Scope::ignored;
  for(;;){
   Span name{};
   if(!str(&name)||name.escaped||!take(':'))return false;
   unsigned flag=0;
   bool valid=true;
   if(scope==Scope::root){
    if(key(name,"schemaVersion")){
     flag=1u<<0;std::uint64_t n=0;
     valid=unsigned_number(&n)&&n==1;
    }else if(key(name,"scope")){
     flag=1u<<1;Span value{};
     valid=str(&value)&&key(value,"bounded-known-roots");
    }else if(key(name,"cleanupValidated")){
     flag=1u<<2;valid=literal("false");
    }else if(key(name,"files")){
     flag=1u<<3;valid=uint_field(&out->files);
    }else if(key(name,"nominalBytes")){
     flag=1u<<4;valid=unsigned_number(&out->bytes);
    }else if(key(name,"partial")){
     flag=1u<<5;
     if(literal("true"))out->partial=true;
     else {out->partial=false;valid=literal("false");}
    }else if(key(name,"categories")){
     flag=1u<<6;valid=object(Scope::categories,out,depth+1);
    }else if(key(name,"review")){
     flag=1u<<7;valid=object(Scope::review,out,depth+1);
    }else valid=ignore(depth+1);
   }else if(scope==Scope::categories){
    if(key(name,"media")){flag=1u<<0;valid=uint_field(&out->media);}
    else if(key(name,"logs")){flag=1u<<1;valid=uint_field(&out->logs);}
    else if(key(name,"cache")){flag=1u<<2;valid=uint_field(&out->cache);}
    else valid=ignore(depth+1);
   }else if(scope==Scope::review){
    if(key(name,"largeMedia")){flag=1u<<0;valid=uint_field(&out->large);}
    else if(key(name,"oldMedia")){flag=1u<<1;valid=uint_field(&out->old);}
    else if(key(name,"oldLargeMedia")){flag=1u<<2;valid=uint_field(&out->old_large);}
    else if(key(name,"largeMediaNominalBytes")){
     flag=1u<<3;valid=unsigned_number(&out->large_bytes);
    }else if(key(name,"validatedForDeletion")){
     flag=1u<<4;valid=literal("false");
    }else valid=ignore(depth+1);
   }else valid=ignore(depth+1);
   if(!valid||(flag&&(mask&flag)))return false;
   mask|=flag;
   if(take('}'))break;
   if(!take(','))return false;
  }
  return scope==Scope::root?mask==255u:
   scope==Scope::categories?mask==7u:
   scope==Scope::review?mask==31u:true;
 }
public:
 OverviewReader(const char*json,std::size_t len) noexcept:at(json),end(json+len){}
 bool parse(Overview*out) noexcept {
  if(!out||!object(Scope::root,out,0))return false;
  whitespace();
  if(at!=end||out->media>out->files||out->cache>out->files||
     out->logs>out->files||out->large>out->media||out->old>out->media||
     out->old_large>out->large||out->old_large>out->old||
     out->large_bytes>out->bytes)return false;
  const std::uint64_t known=static_cast<std::uint64_t>(out->media)+
     out->logs+out->cache;
  return known<=out->files;
 }
};
inline bool parse_overview(const char*json,std::size_t length,Overview*out) noexcept {
 if(!json||!out||length<64||length>4096)return false;
 Overview value{};
 if(!OverviewReader(json,length).parse(&value))return false;
 *out=value;
 return true;
}
} // namespace cmp5_native
