#ifndef CMP5_VERIFIED_BACKUP_H
#define CMP5_VERIFIED_BACKUP_H
#include <stddef.h>
#include <stdint.h>
#include "inventory.h"
#ifdef __cplusplus
extern "C" {
#endif
#define CMP5_BACKUP_LIMIT (512u * 1024u * 1024u)
typedef struct {uint64_t bytes;uint8_t sha256[32];int published;} Cmp5BackupReceipt;
/* Explicit app-owned, 0700 pre-created workspace. Source is never modified.
 * Backups are self-verifying binary containers, not gallery-deletion approval. */
int cmp5_backup_media(const char *source_root,const Cmp5MediaCandidate *candidate,
 const char *private_workspace,const char *backup_id,Cmp5BackupReceipt *receipt,
 Cmp5CancelCheck cancelled,void *context);
int cmp5_verify_backup(const char *private_workspace,const char *backup_id,
 Cmp5BackupReceipt *receipt,Cmp5CancelCheck cancelled,void *context);
/* Read-only rehash of an independent raw restore against its capsule;
 * the PS5 build fixes BOTH workspaces and its RAW_CAP_ name. It never
 * creates, removes or modifies any file, and does not validate Sony indexes. */
int cmp5_verify_rehearsed_backup(const char *private_workspace,
 const char *backup_id,const char *restore_workspace,
 const char *restore_name,Cmp5BackupReceipt *receipt,
 Cmp5CancelCheck cancelled,void *context);
/* Restore to a separate app-owned workspace, NEVER the PS5 gallery. */
int cmp5_restore_backup(const char *private_workspace,const char *backup_id,
 const char *restore_workspace,const char *restore_name,
 Cmp5BackupReceipt *receipt,Cmp5CancelCheck cancelled,void *context);
#ifdef __cplusplus
}
#endif
#endif
