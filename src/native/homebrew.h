#ifndef CMP5_HOMEBREW_H
#define CMP5_HOMEBREW_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef CMP5_CANCEL_CHECK_TYPE
#define CMP5_CANCEL_CHECK_TYPE
typedef int (*Cmp5CancelCheck)(void *context);
#endif
#define CMP5_HOMEBREW_TOP_CAP 4u
/* Categories: 1=homebrew, 2=plugin, 3=app-data, 4=installed.
 * Labels are local-only, printable ASCII (not paths or identifiers of users). */
typedef struct {
 uint64_t nominal_bytes;
 uint32_t files;
 uint32_t partial;
 uint32_t category;
 char label[48];
} Cmp5HomebrewTop;
typedef struct {
 uint32_t entries;
 uint32_t homebrew;
 uint32_t plugins;
 uint32_t app_data;
 uint32_t installed;
 uint32_t partial;
 uint64_t nominal_bytes; /* Seen data only, NOT reclaimable space. */
 uint32_t top_count;
 Cmp5HomebrewTop top[CMP5_HOMEBREW_TOP_CAP];
 /* Principal root coverage: 0=not scanned, 1=readable, 2=absent,
  * 3=permission denied, 4=other error. A zero count is not proof of
  * an empty PS5 library unless BOTH principal roots were traversed. */
 uint32_t data_root_state;
 uint32_t installed_root_state;
} Cmp5HomebrewStats;
/* Read-only bounded inventory of known homebrew and plugin directories. Caller frees output. */
char *cmp5_homebrew(const char *root, size_t *length);
char *cmp5_homebrew_with_stats(const char *root, size_t *length,
 Cmp5HomebrewStats *stats, Cmp5CancelCheck should_cancel, void *context);
#ifdef __cplusplus
}
#endif
#endif
