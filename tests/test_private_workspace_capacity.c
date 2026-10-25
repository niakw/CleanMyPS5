/* Deterministic host-only capacity gate. No files are created. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/mman.h>
static struct statvfs simulated;
static int fail_mount=0,calls=0;
static int synthetic_fstatvfs(int fd,struct statvfs*out){
 (void)fd;calls++;
 if(fail_mount){errno=EIO;return -1;}
 *out=simulated;
 return 0;
}
#define fstatvfs synthetic_fstatvfs
#include "../src/native/private_workspace_guard.h"
#undef fstatvfs
int main(void){
 /* Pathname parsing is independent of any privileged filesystem access.
  * Exactly PATH_MAX bytes without NUL must not probe the protected page. */
 assert(cmp5_workspace_name_ok("/tmp/cmp5-private"));
 assert(!cmp5_workspace_name_ok("/tmp/../system"));
 assert(!cmp5_workspace_name_ok("/tmp//"));
 const long raw_page=sysconf(_SC_PAGESIZE);
 assert(raw_page>0);
 const size_t page=(size_t)raw_page;
 const size_t usable=((size_t)PATH_MAX+page-1u)/page*page;
 void*buffer=mmap(NULL,usable+page,PROT_READ|PROT_WRITE,
                  MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 assert(buffer!=MAP_FAILED);
 assert(mprotect((char*)buffer+usable,page,PROT_NONE)==0);
 char*unterminated=(char*)buffer+usable-(size_t)PATH_MAX;
 memset(unterminated,'A',(size_t)PATH_MAX);
 unterminated[0]='/';
 assert(!cmp5_workspace_name_ok(unterminated));
 assert(munmap(buffer,usable+page)==0);
 memset(&simulated,0,sizeof(simulated));
 simulated.f_frsize=4096u;
 simulated.f_bavail=1024u;
 assert(cmp5_workspace_has_capacity(7,1048576u,2u));
 assert(calls==1);
 assert(!cmp5_workspace_has_capacity(-1,1u,1u));
 assert(!cmp5_workspace_has_capacity(7,0u,1u));
 assert(!cmp5_workspace_has_capacity(7,513ULL*1024ULL*1024ULL,2u));
 assert(!cmp5_workspace_has_capacity(7,1u,0u));
 assert(!cmp5_workspace_has_capacity(7,1u,7u));
 assert(calls==1);
 const uint64_t payload=1048576u;
 const uint64_t reserve=4096u*2u+1048576u+576u;
 const uint64_t blocks=(payload+reserve+4095u)/4096u;
 simulated.f_bfree=UINT32_MAX;
 simulated.f_bavail=blocks-1u;
 assert(!cmp5_workspace_has_capacity(7,payload,2u));
 simulated.f_bavail=blocks;
 assert(cmp5_workspace_has_capacity(7,payload,2u));
 /* Full four-media album: capsules(4) + private origins(1) + COMMIT(1).
  * At the exact threshold, five outputs fit and six do not. */
 const uint64_t complete_reserve=4096u*6u+1048576u+576u;
 const uint64_t complete_blocks=(payload+complete_reserve+4095u)/4096u;
 simulated.f_bavail=complete_blocks-1u;
 assert(cmp5_workspace_has_capacity(7,payload,5u));
 assert(!cmp5_workspace_has_capacity(7,payload,6u));
 simulated.f_bavail=complete_blocks;
 assert(cmp5_workspace_has_capacity(7,payload,6u));
 simulated.f_flag=ST_RDONLY;
 assert(!cmp5_workspace_has_capacity(7,payload,2u));
 simulated.f_flag=0u;
 fail_mount=1;
 assert(!cmp5_workspace_has_capacity(7,payload,2u));
 fail_mount=0;
 simulated.f_frsize=0u;
 assert(!cmp5_workspace_has_capacity(7,payload,2u));
 simulated.f_frsize=UINT64_MAX;
 simulated.f_bavail=UINT32_MAX;
 assert(!cmp5_workspace_has_capacity(7,payload,5u));
 puts("Backup and restore capacity safety gate: PASS");
 return 0;
}
