#ifndef CMP5_VERIFIED_GROUP_H
#define CMP5_VERIFIED_GROUP_H
#include "verified_backup.h"
#include "capture_companions.h"
#ifdef __cplusplus
extern "C" {
#endif
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
/* HOST-ONLY recoverable origin mapping. Stored only in the 0700 private
 * backup workspace, never in a UI report or transmitted to the console.
 * The map binds each ordered backup capsule to its original *locator*,
 * type and length. It does not prove Gallery index/database completeness
 * and provides NO privilege to restore into /user or delete originals. */
typedef struct {
 char path[256];
 uint64_t bytes;
 uint32_t media_type;
} Cmp5OriginEntry;
typedef struct {
 uint32_t count;
 Cmp5OriginEntry entries[CMP5_GROUP_MAX];
} Cmp5OriginMap;
int cmp5_read_group_origin_map(const char *private_workspace,
 const char *group_id,Cmp5OriginMap *result,
 Cmp5CancelCheck cancel,void *context);
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
 uint32_t unexpected_entries; /* namespace collisions or surplus capsules */
 int marker_present;
} Cmp5RecoveryReport;
int cmp5_inspect_group_recovery(const char *private_workspace,
 const char *group_id,Cmp5RecoveryReport *report);
/* Identical read-only audit for a different private restore directory.
 * VERIFIED checks raw restored objects against the RESTORED marker, not the
 * backup capsules. It does not update or validate the PS5 album database. */
int cmp5_inspect_restored_group_recovery(const char *restore_workspace,
 const char *group_id,Cmp5RecoveryReport *report);
/* Host-only, read-only end-to-end audit: verify both private namespaces
 * independently, then compare the COMPLETE group receipts, including the
 * ordered content digests. A matching receipt proves that the restored
 * private bytes correspond to the selected private backup, NOT that PS5
 * gallery indexes/miniatures would be restorable.
 * Distinct workspaces are mandatory even if their paths are aliases. */
typedef enum {
 CMP5_PAIR_UNVERIFIABLE=0,
 CMP5_PAIR_NOT_READY=1,
 CMP5_PAIR_MATCH=2,
 CMP5_PAIR_MISMATCH=3,
 CMP5_PAIR_INVALID=4
} Cmp5RecoveryPairState;
typedef struct {
 Cmp5RecoveryPairState state;
 Cmp5RecoveryReport backup;
 Cmp5RecoveryReport restored;
} Cmp5RecoveryPairReport;
int cmp5_inspect_backup_restore_pair(const char *backup_workspace,
 const char *backup_id,const char *restore_workspace,
 const char *restore_id,Cmp5RecoveryPairReport *report);
#ifdef __cplusplus
}
#endif
#endif
