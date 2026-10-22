#ifndef CMP5_DUPLICATES_H
#define CMP5_DUPLICATES_H
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
/* Anonymous, bounded statistics; bytes are NOMINAL and never a deletion budget. */
typedef struct {
 uint32_t exact_groups;
 uint32_t matching_files;
 uint32_t files_considered;
 uint32_t hardlinks_skipped;
 uint32_t partial;
 uint64_t compared_bytes;
 uint64_t duplicate_nominal_bytes;
} Cmp5DuplicateStats;

/* Exact, bounded, read-only screenshot comparison. Caller frees JSON. */
char *cmp5_duplicates(const char *root, size_t *length);
/* Same scan, returns privacy-safe counters for the native PS5 title. */
char *cmp5_duplicates_with_stats(const char *root, size_t *length, Cmp5DuplicateStats *stats);
/* Cancel-aware variant, no deletion or mutation; partial when cancelled. */
char *cmp5_duplicates_with_stats_cancel(const char *root, size_t *length, Cmp5DuplicateStats *stats, Cmp5CancelCheck should_cancel, void *context);
#ifdef __cplusplus
}
#endif
#endif
