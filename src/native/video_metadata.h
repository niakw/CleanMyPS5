#ifndef CMP5_VIDEO_METADATA_H
#define CMP5_VIDEO_METADATA_H
#include <stdint.h>
#include "inventory.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Metadata only: MP4 moov/mvhd/tkhd, never decodes or writes media.
 * A false return means unsupported/unavailable/modified/cancelled. */
typedef struct {
 uint64_t duration_ms;
 uint32_t width;
 uint32_t height;
 uint32_t boxes_visited;
} Cmp5VideoMetadata;
int cmp5_probe_video_snapshot(const char *root,
 const Cmp5MediaCandidate *candidate,Cmp5VideoMetadata *out,
 Cmp5CancelCheck should_cancel,void *context);
#ifdef __cplusplus
}
#endif
#endif
