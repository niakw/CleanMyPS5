/* CleanMyPS5 / GPL-3.0-or-later. Canonical fingerprint of AMPR's
 * FINAL source-file action plan, after profile rules and block budgeting.
 * Requires pinned engine scan.h / SHA256 implementation.  The header is
 * compiled in BOTH the PS5 worker and the host regression fixture. */
#ifndef CMP5_AMPR_CHECKPOINT_PLAN_H
#define CMP5_AMPR_CHECKPOINT_PLAN_H
#include "scan.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
static void cmp5_plan_hash_u64(e2a_sha256 *hash,uint64_t value){
  unsigned char little[8];
  for(unsigned i=0;i<8u;i++)little[i]=(unsigned char)(value>>(i*8u));
  e2a_sha256_update(hash,little,sizeof little);
}
static int cmp5_entries_plan_hash(const e2a_list *entries,char hex[65]){
  if(!entries||!hex||(entries->n && !entries->v))return -1;
  e2a_sha256 hash;
  e2a_sha256_init(&hash);
  static const char tag[]="CMP5-FILE-PLAN-v1";
  e2a_sha256_update(&hash,tag,sizeof tag);
  cmp5_plan_hash_u64(&hash,(uint64_t)entries->n);
  for(size_t i=0;i<entries->n;i++){
    const e2a_entry *item=&entries->v[i];
    if(!item->rel)return -1;
    const size_t len=strlen(item->rel);
    if(len>65535u)return -1;
    cmp5_plan_hash_u64(&hash,(uint64_t)len);
    e2a_sha256_update(&hash,item->rel,len);
    cmp5_plan_hash_u64(&hash,item->size);
    /* Synthetic runtime files are reinstalled and may obtain a fresh
     * mtime. Their filenames, sizes and actions still belong to the plan. */
    cmp5_plan_hash_u64(&hash,item->synthetic?0u:(uint64_t)item->mtime);
    unsigned char flags[5]={item->action,item->block_shift,
      item->layout,(unsigned char)item->hot,
      (unsigned char)item->synthetic};
    e2a_sha256_update(&hash,flags,sizeof flags);
  }
  unsigned char digest[32];
  e2a_sha256_final(&hash,digest);
  static const char chars[]="0123456789abcdef";
  for(size_t i=0;i<32u;i++){
    hex[2u*i]=chars[digest[i]>>4];
    hex[2u*i+1u]=chars[digest[i]&15u];
  }
  hex[64]=0;
  return 0;
}
#endif
