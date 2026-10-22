#ifndef CMP5_CAPTURE_COMPANIONS_H
#define CMP5_CAPTURE_COMPANIONS_H
#include "inventory.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Discovery only. Never copies, renames or deletes PS5 media.
 * A group of matching visible files is not proof of a complete PS5 album. */
#define CMP5_GROUP_MAX 4u
int cmp5_plan_capture_group(const char *root,const Cmp5MediaCandidate *primary,
 Cmp5MediaCandidate out[CMP5_GROUP_MAX],uint32_t *count,
 Cmp5CancelCheck cancel,void *context);
#ifdef __cplusplus
}
#endif
#endif
