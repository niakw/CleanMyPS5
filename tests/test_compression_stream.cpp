// Synthetic ELF only: verify loader stream never handles other content.
#include "../native/compression_stream.hpp"
#include <cassert>
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
 data[4]=2;data[5]=1;data[18]=0x3e;
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
 assert(close(fd)==0);
 assert(unlink(path)==0);
 puts("Embedded PS5 ELF stream: bounded, cancelable, no untrusted source: PASS");
}
