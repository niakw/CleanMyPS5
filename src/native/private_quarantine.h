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
/* Restore the held private file to its exact, original laboratory name,
 * without overwriting. Require intact independent backup evidence again. */
int cmp5_private_lab_recover(const char*lab,const char*item,
 const char*backup,const char*backup_id);
#ifdef __cplusplus
}
#endif
#endif
