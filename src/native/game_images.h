#ifndef CMP5_GAME_IMAGES_H
#define CMP5_GAME_IMAGES_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef CMP5_CANCEL_CHECK_TYPE
#define CMP5_CANCEL_CHECK_TYPE
typedef int (*Cmp5CancelCheck)(void* context);
#endif
enum { CMP5_IMAGE_EXFAT=0, CMP5_IMAGE_FFPKG=1,
 CMP5_IMAGE_FFPFS=2, CMP5_IMAGE_FFPFSC=3, CMP5_IMAGE_FORMATS=4 };
typedef struct {
 uint32_t scanned_roots,missing_roots,unavailable_roots,entries_seen;
 uint32_t count[CMP5_IMAGE_FORMATS];
 uint32_t large_candidates; /* uncompressed >=20 GiB, NOT verified playable */
 uint32_t partial, hardlinks_skipped;
 uint64_t nominal_bytes,large_nominal_bytes,largest_bytes;
 uint32_t folder_games,large_folders,folder_partials;
 uint64_t folder_nominal_bytes,large_folder_nominal_bytes;
} Cmp5GameImageStats;
/* Bounded, metadata-only game IMAGE survey. Fixed roots, no filenames or
 * paths leave the scanner; no image contents, payloads or writes are opened.
 * Absence of results does not prove absence of installed PS5 games.
 * A compression operation requires a separate compatible engine and an
 * actual console launch check; this API never authorizes deletion. */
int cmp5_scan_game_images(const char*root,Cmp5GameImageStats*result,
 Cmp5CancelCheck cancel,void*context);
#ifdef __cplusplus
}
#endif
#endif
