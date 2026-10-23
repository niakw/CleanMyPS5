// CleanMyPS5: stream a bundled ELF into the active local loader without
// exposing user-selected paths. GPL-3.0-or-later. Host tested, PS5 untested.
#pragma once
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
namespace cmp5_compression {
inline bool elf_header(const unsigned char*head,std::size_t n) noexcept {
 return n>=20&&head[0]==0x7f&&head[1]=='E'&&head[2]=='L'&&
  head[3]=='F'&&head[4]==2&&head[5]==1&&
  head[18]==0x3e&&head[19]==0;
}
inline bool same_file(const struct stat&a,const struct stat&b) noexcept {
#if defined(__APPLE__)
 const bool ns=a.st_mtimespec.tv_nsec==b.st_mtimespec.tv_nsec&&
   a.st_ctimespec.tv_nsec==b.st_ctimespec.tv_nsec;
#else
 const bool ns=a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&
   a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
#endif
 return ns&&S_ISREG(a.st_mode)&&S_ISREG(b.st_mode)&&
  a.st_nlink==1&&b.st_nlink==1&&a.st_dev==b.st_dev&&
  a.st_ino==b.st_ino&&a.st_size==b.st_size&&
  a.st_mtime==b.st_mtime&&a.st_ctime==b.st_ctime&&
  a.st_mode==b.st_mode;
}
template<typename Sink,typename Cancel>
bool stream_embedded_elf(int fd,Sink sink,Cancel cancelled,
                         std::uint64_t*transferred=nullptr) noexcept {
 if(transferred)*transferred=0;
 if(fd<0)return false;
 struct stat before{},after{};
 if(fstat(fd,&before)<0||!S_ISREG(before.st_mode)||before.st_nlink!=1||
    before.st_size<4096||static_cast<std::uint64_t>(before.st_size)>64u*1024u*1024u)
  return false;
 unsigned char header[20]{};
 ssize_t r;
 do{r=read(fd,header,sizeof(header));}while(r<0&&errno==EINTR);
 if(r!=static_cast<ssize_t>(sizeof(header))||!elf_header(header,sizeof(header))||
    lseek(fd,0,SEEK_SET)!=0)return false;
 unsigned char block[32768]{};
 std::uint64_t amount=0;
 while(amount<static_cast<std::uint64_t>(before.st_size)){
  if(cancelled())return false;
  auto ask=sizeof(block);
  const auto remaining=static_cast<std::uint64_t>(before.st_size)-amount;
  if(remaining<ask)ask=static_cast<std::size_t>(remaining);
  do{r=read(fd,block,ask);}while(r<0&&errno==EINTR);
  if(r<=0)return false;
  std::size_t sent=0;
  while(sent<static_cast<std::size_t>(r)){
   if(cancelled())return false;
   const int n=sink(block+sent,static_cast<std::size_t>(r)-sent);
   if(n<=0||static_cast<std::size_t>(n)>static_cast<std::size_t>(r)-sent)
    return false;
   sent+=static_cast<std::size_t>(n);
  }
  amount+=static_cast<std::uint64_t>(r);
 }
 if(fstat(fd,&after)!=0||!same_file(before,after)||cancelled())return false;
 if(transferred)*transferred=amount;
 return amount==static_cast<std::uint64_t>(before.st_size);
}
}
