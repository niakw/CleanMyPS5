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
/* Read-only companion discovery from the exact snapshotted capture directory.
 * Refuses symlinks, incomplete directory scans, ambiguous stems or >4 variants.
 * Caller-provided primary is never modified. Not an approval for PS5 deletion. */
int cmp5_plan_capture_group(const char *root,const Cmp5MediaCandidate *primary,
 Cmp5MediaCandidate out[CMP5_GROUP_MAX],uint32_t *count,
 Cmp5CancelCheck cancel,void *context);
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
/* Read-only recovery audit of the private backup workspace.
 * EMPTY: no known components; INCOMPLETE: no committed marker;
 * VERIFIED: marker and all capsules pass SHA-256; INVALID: a marker is
 * corrupt/incomplete or an unexpected non-regular/linked entry is found.
 * No staging file, capsule, or gallery item is ever changed or removed. */
typedef enum {
 CMP5_RECOVERY_EMPTY=1,
 CMP5_RECOVERY_INCOMPLETE=2,
 CMP5_RECOVERY_VERIFIED=3,
 CMP5_RECOVERY_INVALID=4
} Cmp5RecoveryState;
typedef struct {
 Cmp5RecoveryState state;
 uint32_t published_parts;
 uint32_t staged_parts;
 int marker_present;
} Cmp5RecoveryReport;
int cmp5_inspect_group_recovery(const char *private_workspace,
 const char *group_id,Cmp5RecoveryReport *report);
#ifdef __cplusplus
}
#endif
#endif
