#ifndef CMP5_PREVIEW_DECODE_H
#define CMP5_PREVIEW_DECODE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
 uint32_t width,height;
 uint32_t *pixels; /* Packed ABGR / native little-endian RGBA */
} Cmp5PreviewImage;
/* Decode and resample JPEG/PNG photos, with strict dimensions/size limits.
 * The output is UI-only; callers must free via cmp5_release_preview(). */
int cmp5_decode_preview(const uint8_t *data,size_t length,Cmp5PreviewImage *out);
void cmp5_release_preview(Cmp5PreviewImage *image);
#ifdef __cplusplus
}
#endif
#endif
