/* PS5-native compile mode: source and destination for raw private
 * restoration must be fixed. This NEVER writes to a PS5 or host Gallery.
 * Host functional restoration with real files lives in test_verified_backup. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "verified_backup.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
 Cmp5BackupReceipt receipt={0};
 const char*id="CAP_0123456789ABCDEF";
 const char*raw="RAW_CAP_0123456789ABCDEF";
 /* Wrong destinations and names must fail before opening any workspace. */
 assert(!cmp5_restore_backup("/tmp/private-backups",id,
       "/download0/cmp5-private-rehearsals",raw,&receipt,NULL,NULL));
 assert(!cmp5_restore_backup("/download0/cmp5-private-backups",id,
       "/user/av_contents/photo",raw,&receipt,NULL,NULL));
 assert(!cmp5_restore_backup("/download0/cmp5-private-backups",id,
       "/download0/cmp5-private-rehearsals",
       "RAW_CAP_0123456789ABCDE0",&receipt,NULL,NULL));
 assert(!cmp5_restore_backup("/download0/cmp5-private-backups",
       "CAP_0123456789ABCDEg",
       "/download0/cmp5-private-rehearsals",raw,&receipt,NULL,NULL));
 assert(!cmp5_restore_backup("/download0/cmp5-private-backups",
       "GRP_0123456789ABCDEF",
       "/download0/cmp5-private-rehearsals",raw,&receipt,NULL,NULL));
 assert(!cmp5_restore_backup("/download0/cmp5-private-backups",id,
       "/download0/cmp5-private-rehearsals","CAP_0123456789ABCDEF",
       &receipt,NULL,NULL));
 /* On this host, /download0 is NOT a fabricated test path and no
  * directories should be created by invoking the low-level function. */
 assert(!cmp5_restore_backup(NULL,id,
       "/download0/cmp5-private-rehearsals",raw,&receipt,NULL,NULL));
 assert(!receipt.published);
 puts("Native private rehearsal scope: PASS");
 return 0;
}
