// CleanMyPS5 native client: strict, allocation-free HTTP response framing.
// GPL-3.0-or-later. Only a bounded application/json response is accepted.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cmp5_native {
struct HttpBody {
 const char* data=nullptr;
 std::size_t size=0;
};

inline bool parse_http_json(const char*buffer,std::size_t size,HttpBody*out) noexcept {
 if(out)*out={};
 if(!buffer||!out||size<32)return false;
 constexpr char prefix[]="HTTP/1.1 200 ";
 if(size<sizeof(prefix)-1||
    std::memcmp(buffer,prefix,sizeof(prefix)-1)!=0)return false;
 constexpr std::size_t max_header=2048;
 std::size_t header_end=0;
 bool found=false;
 for(std::size_t i=0;i+3<size&&i<max_header;i++){
  if(std::memcmp(buffer+i,"\r\n\r\n",4)==0){
   header_end=i+4;found=true;break;
  }
 }
 if(!found||header_end>=size)return false;
 const char*status_end=nullptr;
 for(std::size_t i=0;i+1<header_end;i++){
  if(buffer[i]=='\r'&&buffer[i+1]=='\n'){
   status_end=buffer+i;break;
  }
 }
 if(!status_end||status_end-buffer<13)return false;
 const char*cursor=status_end+2;
 const char*headers_end=buffer+header_end-2;
 bool has_length=false,has_type=false;
 std::size_t declared=0;
 while(cursor<headers_end){
  const char*end=nullptr;
  for(const char*p=cursor;p+1<=headers_end;p++){
   if(p+1<buffer+header_end&&p[0]=='\r'&&p[1]=='\n'){
    end=p;break;
   }
  }
  if(!end||end==cursor)return false;
  constexpr char length[]="Content-Length: ";
  constexpr char type[]="Content-Type: ";
  constexpr char encoding[]="Transfer-Encoding:";
  const std::size_t line_size=static_cast<std::size_t>(end-cursor);
  if(line_size>=sizeof(length)-1&&
     std::memcmp(cursor,length,sizeof(length)-1)==0){
   if(has_length)return false;
   has_length=true;
   const char*digits=cursor+sizeof(length)-1;
   if(digits==end)return false;
   for(const char*p=digits;p<end;p++){
    if(*p<'0'||*p>'9')return false;
    const unsigned digit=static_cast<unsigned>(*p-'0');
    if(declared>(65536u-digit)/10u)return false;
    declared=declared*10u+digit;
   }
  }else if(line_size>=sizeof(type)-1&&
           std::memcmp(cursor,type,sizeof(type)-1)==0){
   if(has_type)return false;
   has_type=true;
   constexpr char expected[]="application/json";
   const char*value=cursor+sizeof(type)-1;
   const std::size_t remaining=static_cast<std::size_t>(end-value);
   if(remaining<sizeof(expected)-1||
      std::memcmp(value,expected,sizeof(expected)-1)!=0)return false;
   if(remaining>sizeof(expected)-1&&value[sizeof(expected)-1]!=';')return false;
  }else if(line_size>=sizeof(encoding)-1&&
           std::memcmp(cursor,encoding,sizeof(encoding)-1)==0){
   return false; // No chunked or ambiguous framing.
  }
  cursor=end+2;
 }
 if(cursor!=headers_end||!has_length||!has_type||declared==0)return false;
 if(declared!=size-header_end)return false; // Reject partial and trailing data.
 if(buffer[header_end]!='{')return false;
 out->data=buffer+header_end;
 out->size=declared;
 return true;
}
} // namespace cmp5_native
