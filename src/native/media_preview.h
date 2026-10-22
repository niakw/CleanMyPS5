#ifndef CMP5_MEDIA_PREVIEW_H
#define CMP5_MEDIA_PREVIEW_H
#include <stddef.h>
#include <stdint.h>
#include "inventory.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Reads one selected, snapshotted gallery PHOTO, not arbitrary paths.
 * Uses only descriptor-anchored O_NOFOLLOW opens; caller frees returned bytes.
 * No write calls, no gallery database updates, no external server. */
#define CMP5_PREVIEW_FILE_CAP (16u * 1024u * 1024u)
uint8_t *cmp5_read_photo_snapshot(const char *root,
 const Cmp5MediaCandidate *candidate,size_t *length);
#ifdef __cplusplus
}
#endif
#endif
