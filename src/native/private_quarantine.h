/* Host-only, explicitly named private laboratory quarantine.
 * NOT included in the PS5 title or diagnostic helper. GPL-3.0-or-later. */
#ifndef CMP5_PRIVATE_QUARANTINE_H
#define CMP5_PRIVATE_QUARANTINE_H
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
 CMP5_LAB_INVALID=0,
 CMP5_LAB_EMPTY=1,
 CMP5_LAB_SOURCE_ONLY=2,
 CMP5_LAB_HELD_ONLY=3,
 CMP5_LAB_AMBIGUOUS=4
} Cmp5LabState;
/* An interrupted double-name transition is never resolved automatically.
 * Explicitly select the final name only after an independent SHA-256 check
 * proves BOTH hardlinks are the same original file (nlink == 2). */
typedef enum {
 CMP5_LAB_KEEP_SOURCE=1,
 CMP5_LAB_KEEP_HELD=2
} Cmp5LabResolution;
/* Host-only simulated crash checkpoint, invoked after both hardlinks have
 * been fsynced and BEFORE unlink. A nonzero result leaves both names
 * intact. Tests may terminate their child process at this checkpoint. */
typedef int (*Cmp5LabCheckpoint)(void*context);
/* Identifies source/held names without changing the filesystem. */
Cmp5LabState cmp5_private_lab_state(const char*lab,const char*item);
/* A complete SHA-256 verified backup and a successful restoration rehearsal
 * in a SEPARATE private workspace are required before holding an item.
 * This never saves space: a hold is a reversible move within one private
 * laboratory. No console root, game, capture, plugin or system cache is
 * eligible. Any interrupted two-name transition is ambiguous, not auto-fixed. */
int cmp5_private_lab_hold(const char*lab,const char*item,
 const char*backup,const char*backup_id,
 const char*rehearsal,const char*rehearsal_id);
int cmp5_private_lab_hold_checkpoint(const char*lab,const char*item,
 const char*backup,const char*backup_id,
 const char*rehearsal,const char*rehearsal_id,
 Cmp5LabCheckpoint checkpoint,void*context);
/* Restore the held private file to its exact, original laboratory name,
 * without overwriting. Require intact independent backup evidence again. */
int cmp5_private_lab_recover(const char*lab,const char*item,
 const char*backup,const char*backup_id);
int cmp5_private_lab_recover_checkpoint(const char*lab,const char*item,
 const char*backup,const char*backup_id,
 Cmp5LabCheckpoint checkpoint,void*context);
/* The caller must deliberately choose a resolution. No guessing, no unlink
 * if the entries are different, one is missing/symlinked, or hash differs
 * from the independent verified capsule. Lab-only: no PS5 writes. */
int cmp5_private_lab_resolve_interrupted(const char*lab,const char*item,
 const char*backup,const char*backup_id,Cmp5LabResolution resolution);
#ifdef __cplusplus
}
#endif
#endif
