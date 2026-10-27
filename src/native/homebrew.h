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
#define CMP5_CACHE_TOP_CAP 4u
/* 1=cache, 2=.cache, 3=tmp, 4=temp; not deletion categories. */
typedef struct {
 uint64_t nominal_bytes;
 uint32_t files;
 uint32_t partial;
 uint32_t kind;
 uint32_t name_matched;
 uint32_t age_hint; /* 0=unknown/partial, 1=recent 24h, 2=no recent mtime */
 /* Order-independent, metadata-only identity clues for two local scans.
  * Never hash file bodies or regard equal fingerprints as proof of idle. */
 uint64_t metadata_xor;
 uint64_t metadata_sum;
 uint32_t metadata_nodes;
 uint32_t metadata_complete;
 char app_label[48]; /* ASCII basename only; no paths persisted */
} Cmp5CacheTop;
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
 /* Bounded second look at /data/<app>/{cache,.cache,tmp,temp}.
  * Folder naming does NOT prove provider ownership or safe deletion.
  * Never add this nominal subtotal to the general storage total. */
 uint32_t cache_like_folders;
 uint32_t cache_top_count;
 Cmp5CacheTop cache_top[CMP5_CACHE_TOP_CAP];
 uint32_t cache_like_files;
 uint32_t cache_like_partial;
 uint64_t cache_like_nominal_bytes;
 /* Strict case-sensitive basename matches to a separately observed
  * /data/homebrew/<name>/eboot.elf or eboot.bin. These are NEVER verified
  * owners, references, inactive processes, or deletion permission. */
 uint32_t cache_like_name_matched;
 uint32_t cache_like_name_unmatched;
 uint32_t homebrew_name_index_partial;
 /* mtime-based hints only. These three categories are exclusive and
  * total cache_like_folders, but none proves an application is idle. */
 uint32_t cache_mtime_recent;
 uint32_t cache_mtime_unknown;
 uint32_t cache_mtime_no_recent;
} Cmp5HomebrewStats;
/* Read-only bounded inventory of known homebrew and plugin directories. Caller frees output. */
char *cmp5_homebrew(const char *root, size_t *length);
char *cmp5_homebrew_with_stats(const char *root, size_t *length,
 Cmp5HomebrewStats *stats, Cmp5CancelCheck should_cancel, void *context);
#ifdef __cplusplus
}
#endif
#endif
