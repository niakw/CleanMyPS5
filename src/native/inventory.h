#ifndef CMP5_INVENTORY_H
#define CMP5_INVENTORY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Cooperative cancellation callback. Must not mutate scanner-owned objects. */
#ifndef CMP5_CANCEL_CHECK_TYPE
#define CMP5_CANCEL_CHECK_TYPE
typedef int (*Cmp5CancelCheck)(void *context);
#endif
#define CMP5_REVIEW_CAP 10000u
/* Bounded native media catalog (up to 10,000 scanned media, approx. 4 MiB).
 * Display labels are ASCII, not paths or filesystem handles. No element is
 * validated for deletion; this is read-only and never scans beyond limits. */
typedef struct {
 uint64_t bytes;
 int64_t modified_at;
 uint32_t media_type; /* 1=image, 2=video */
 char title_id[24];
 char game_name[56]; /* sanitized optional title from a captured .dat sidecar */
 char label[48];
 /* Internal-only local snapshot locator, never serialized in reports or UI text.
  * Empty means preview unavailable. Validate inode/size/mtime after open. */
 char preview_path[256];
 uint64_t snapshot_device;
 uint64_t snapshot_inode;
} Cmp5MediaCandidate;
typedef struct {
 uint32_t count;
 uint32_t partial;
 uint32_t media_scanned;
 Cmp5MediaCandidate entries[CMP5_REVIEW_CAP];
} Cmp5MediaReview;
/* JSON report; read-only. Caller owns returned buffer and must free(). */
char *cmp5_report(const char *root, size_t *len);
/* Anonymous aggregate, same bounds as full report; does not expose paths. */
char *cmp5_overview(const char *root, size_t *len);
/* Same bounded scan, optionally returns media labels sorted largest first.
 * Caller allocates Cmp5MediaReview outside the thread stack (e.g. static/heap).
 * count <= CMP5_REVIEW_CAP; partial describes the filesystem scan coverage. */
char *cmp5_overview_with_review(const char *root, size_t *len, Cmp5MediaReview *review);
/* Cancel-aware variant: returns a partial scan; caller must reject cancelled results. */
char *cmp5_overview_with_review_cancel(const char *root, size_t *len, Cmp5MediaReview *review, Cmp5CancelCheck should_cancel, void *context);
#ifdef __cplusplus
}
#endif
#endif
