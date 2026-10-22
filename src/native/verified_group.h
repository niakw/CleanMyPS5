#ifndef CMP5_VERIFIED_GROUP_H
#define CMP5_VERIFIED_GROUP_H
#include "verified_backup.h"
#ifdef __cplusplus
extern "C" {
#endif
#define CMP5_GROUP_MAX 4u
/* A logical transaction: commit marker published only AFTER all private
 * component backup capsules pass SHA-256 verification. No PS5 gallery writes. */
typedef struct {
 uint32_t count;
 uint64_t bytes;
 uint8_t manifest_sha256[32];
} Cmp5GroupReceipt;
int cmp5_backup_group(const char *root,const Cmp5MediaCandidate *parts,
 uint32_t count,const char *private_workspace,const char *group_id,
 Cmp5GroupReceipt *result,Cmp5CancelCheck cancel,void *context);
int cmp5_verify_group(const char *private_workspace,const char *group_id,
 Cmp5GroupReceipt *result,Cmp5CancelCheck cancel,void *context);
/* Restores into a *different, pre-existing, 0700 private directory*.
 * The __RESTORED commit marker indicates that every raw member was verified.
 * NEVER restores to /user/av_contents or repairs the PS5 gallery database. */
int cmp5_restore_group(const char *private_workspace,const char *group_id,
 const char *restore_workspace,Cmp5GroupReceipt *result,
 Cmp5CancelCheck cancel,void *context);
int cmp5_verify_restored_group(const char *restore_workspace,const char *group_id,
 Cmp5GroupReceipt *result,Cmp5CancelCheck cancel,void *context);
#ifdef __cplusplus
}
#endif
#endif
