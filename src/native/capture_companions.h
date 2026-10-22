#ifndef CMP5_CAPTURE_COMPANIONS_H
#define CMP5_CAPTURE_COMPANIONS_H
#include "inventory.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Discovery only. Never copies, renames or deletes PS5 media.
 * A group of matching visible files is not proof of a complete PS5 album. */
#define CMP5_GROUP_MAX 4u
/* Metadata-only consistency check shared with the host backup prototype.
 * It does not assert that all PS5 gallery/index files have been discovered. */
int cmp5_validate_capture_group(const Cmp5MediaCandidate *parts,uint32_t count);
/* Three-way read-only observation. SINGLE means the primary file was
 * observed and revalidated, not that its PS5 album has no dependencies.
 * MULTIPLE means matching visible companions, not a verified complete album.
 * A failed inspection leaves the report zeroed (UNVERIFIABLE). */
typedef enum {
 CMP5_COMPANIONS_UNVERIFIABLE=0,
 CMP5_COMPANIONS_SINGLE=1,
 CMP5_COMPANIONS_MULTIPLE=2
} Cmp5CompanionState;
typedef struct {
 Cmp5CompanionState state;
 uint32_t files;
 uint32_t media;
 uint32_t sidecars;
 uint64_t nominal_bytes;
} Cmp5CompanionReport;
int cmp5_inspect_capture_companions(const char *root,
 const Cmp5MediaCandidate *primary,Cmp5CompanionReport *report,
 Cmp5CancelCheck cancel,void *context);
/* Legacy backup planner still refuses a one-file group. */
int cmp5_plan_capture_group(const char *root,const Cmp5MediaCandidate *primary,
 Cmp5MediaCandidate out[CMP5_GROUP_MAX],uint32_t *count,
 Cmp5CancelCheck cancel,void *context);
#ifdef __cplusplus
}
#endif
#endif
