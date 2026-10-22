#ifndef CMP5_DIAGNOSTICS_H
#define CMP5_DIAGNOSTICS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Eleven known paths only. Counters describe observed configuration files,
 * not running processes, jailbreak status, or permissions beyond these paths. */
typedef struct {
 uint32_t checked;
 uint32_t present;
 uint32_t missing;
 uint32_t unreadable;
 uint32_t oversized;
 uint32_t etahen_references;
 uint32_t other_references;
 uint32_t autostart_enabled;
 uint32_t autostart_disabled;
 uint32_t no_reference;
 uint32_t unknown;
} Cmp5AutoloadStats;
/* Read-only summary with aggregate counters for the native title.
 * Returns NULL on allocation/serialization failure; caller frees JSON.
 * The JSON contains fixed path labels: never display it in native UI. */
char *cmp5_diagnostics_with_stats(const char *namespace_root, size_t *length,
                                  Cmp5AutoloadStats *stats);
/* Backwards-compatible helper JSON interface. */
char *cmp5_diagnostics(const char *namespace_root, size_t *length);
#ifdef __cplusplus
}
#endif
#endif
