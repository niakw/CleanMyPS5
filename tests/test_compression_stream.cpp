// Synthetic ELF only: verify loader stream never handles other content.
#include "../native/compression_stream.hpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
int main(){
 char folder[]="/tmp/cmp5-elf-XXXXXX";
 int fd=mkstemp(folder);
 assert(fd>=0);
 assert(unlink(folder)==0);
 unsigned char data[4096]{};
 data[0]=0x7f;data[1]='E';data[2]='L';data[3]='F';
 data[4]=2;data[5]=1;data[6]=1;
 data[16]=3;data[18]=0x3e;data[20]=1;
 data[32]=64;data[52]=64;data[54]=56;data[56]=1;
 assert(cmp5_compression::elf_program_headers_fit(data,sizeof(data)));
 assert(write(fd,data,sizeof(data))==(ssize_t)sizeof(data));
 assert(lseek(fd,0,SEEK_SET)==0);
 // Already unlinked files are rejected (no stable named app asset).
 std::uint64_t sent=123;
 assert(!cmp5_compression::stream_embedded_elf(fd,
  [](const unsigned char*,std::size_t n){return static_cast<int>(n);},
  []{return false;},&sent));
 assert(sent==0);
 assert(close(fd)==0);
 char path[]="/tmp/cmp5-elf2-XXXXXX";
 fd=mkstemp(path);assert(fd>=0);
 assert(write(fd,data,sizeof(data))==(ssize_t)sizeof(data));
 assert(lseek(fd,0,SEEK_SET)==0);
 unsigned calls=0;sent=0;
 assert(cmp5_compression::stream_embedded_elf(fd,
  [&](const unsigned char*p,std::size_t n){
   assert(p&&n>0);calls++;return static_cast<int>(n>11?11:n);
  },[]{return false;},&sent));
 assert(sent==sizeof(data)&&calls>200u);
 assert(lseek(fd,0,SEEK_SET)==0);
 assert(!cmp5_compression::stream_embedded_elf(fd,
  [](const unsigned char*,std::size_t){return -1;},[]{return false;}));
 assert(lseek(fd,0,SEEK_SET)==0);
 assert(!cmp5_compression::stream_embedded_elf(fd,
  [](const unsigned char*,std::size_t){return 20;},[]{return true;}));
 assert(lseek(fd,0,SEEK_SET)==0);
 unsigned char corrupt[4096]{};
 std::memcpy(corrupt,data,sizeof(corrupt));
 corrupt[16]=1; // ET_REL: ELF object, not a loadable executable
 assert(!cmp5_compression::elf_header(corrupt,sizeof(corrupt)));
 assert(pwrite(fd,corrupt,sizeof(corrupt),0)==(ssize_t)sizeof(corrupt));
 assert(lseek(fd,0,SEEK_SET)==0);
 unsigned sends=0;
 const auto sink=[&](const unsigned char*,std::size_t n){
  ++sends;return static_cast<int>(n);
 };
 assert(!cmp5_compression::stream_embedded_elf(fd,sink,[]{return false;}));
 assert(sends==0);
 std::memcpy(corrupt,data,sizeof(corrupt));
 corrupt[33]=0x20; // PHDR offset beyond file
 assert(!cmp5_compression::elf_program_headers_fit(corrupt,sizeof(corrupt)));
 assert(pwrite(fd,corrupt,sizeof(corrupt),0)==(ssize_t)sizeof(corrupt));
 assert(lseek(fd,0,SEEK_SET)==0);
 assert(!cmp5_compression::stream_embedded_elf(fd,sink,[]{return false;}));
 assert(sends==0);
 assert(ftruncate(fd,32)==0);
 assert(lseek(fd,0,SEEK_SET)==0);
 assert(!cmp5_compression::stream_embedded_elf(fd,sink,[]{return false;}));
 assert(sends==0);
 assert(close(fd)==0);
 assert(unlink(path)==0);
 puts("Embedded PS5 ELF stream: executable-only, PHDR bounds, zero malformed sends: PASS");
}
