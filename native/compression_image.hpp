// A read-only ShadowMountPlus image identity is required before any AMPR job.
// No title IDs derived from filenames. Copyright (C) 2026 CleanMyPS5.
// GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace cmp5_compression {
struct ImageResolution {
 char title_id[16]{};
 char image_type[16]{};
 std::uint64_t bytes=0;
};
inline bool parse_image_resolution(const char*json,std::size_t n,
                                   ImageResolution*out) noexcept {
 if(!json||!out||n<54||n>160)return false;
 constexpr char head[]="{\"title_id\":\"";
 constexpr char middle[]="\",\"image_type\":\"";
 constexpr char sizes[]="\",\"size\":";
 constexpr std::size_t p=sizeof(head)-1,m=sizeof(middle)-1,z=sizeof(sizes)-1;
 if(n<p+9+m+3+z+2||std::memcmp(json,head,p)!=0)return false;
 ImageResolution candidate{};
 std::memcpy(candidate.title_id,json+p,9);
 for(unsigned i=0;i<9;i++){
  const char c=candidate.title_id[i];
  if(i<4?(c<'A'||c>'Z'):(c<'0'||c>'9'))return false;
 }
 std::size_t at=p+9;
 if(at+m>n||std::memcmp(json+at,middle,m)!=0)return false;
 at+=m;
 std::size_t t=0;
 while(at<n&&json[at]!='"'){
  if(t>=sizeof(candidate.image_type)-1)return false;
  const char c=json[at++];
  if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')))return false;
  candidate.image_type[t++]=c;
 }
 if(at+z>n||std::memcmp(json+at,sizes,z)!=0)return false;
 at+=z;
 if(at>=n-1||json[n-1]!='}'||json[at]=='0')return false;
 std::uint64_t bytes=0;
 for(;at<n-1;at++){
  const char c=json[at];
  if(c<'0'||c>'9')return false;
  const auto digit=static_cast<unsigned>(c-'0');
  if(bytes>(UINT64_MAX-digit)/10u)return false;
  bytes=bytes*10u+digit;
 }
 if(bytes==0||!(std::strcmp(candidate.image_type,"exfatfs")==0||
   std::strcmp(candidate.image_type,"ufs")==0||
   std::strcmp(candidate.image_type,"pfs")==0))return false;
 candidate.bytes=bytes;
 *out=candidate;
 return true;
}
}
