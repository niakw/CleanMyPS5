#ifndef CMP5_INVENTORY_H
#define CMP5_INVENTORY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CMP5_REVIEW_CAP 12u
/* Fixed, read-only metadata shortlist. Labels are ASCII display strings, not
 * paths or filesystem handles. No candidate is authorized for deletion. */
typedef struct {
 uint64_t bytes;
 int64_t modified_at;
 uint32_t media_type; /* 1=image, 2=video */
 char title_id[24];
 char label[48];
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
/* Same bounded scan, optionally returns the 12 largest media labels. */
char *cmp5_overview_with_review(const char *root, size_t *len, Cmp5MediaReview *review);
#ifdef __cplusplus
}
#endif
#endif
