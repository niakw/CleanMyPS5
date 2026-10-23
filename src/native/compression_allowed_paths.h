/* CleanMyPS5: only scan-listed direct game entries and known writable
 * destination roots are accepted by the embedded AMPR job interface.
 * No follow-up path resolution, no permission elevation.
 * GPL-3.0-or-later. Copyright (C) 2026 CleanMyPS5 contributors. */
#ifndef CMP5_COMPRESSION_ALLOWED_PATHS_H
#define CMP5_COMPRESSION_ALLOWED_PATHS_H
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

static inline bool cmp5_safe_component(const char *name) {
 if (!name || !*name || name[0] == '.') return false;
 const size_t n = strlen(name);
 if (n == 0 || n >= 64) return false;
 for (size_t i = 0; i < n; ++i) {
  const unsigned char c = (unsigned char)name[i];
  if (!((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '_' || c == '-' || c == '.' || c == ' ' ||
        c == '[' || c == ']' || c == '(' || c == ')'))
   return false;
 }
 return true;
}
static inline bool cmp5_has_prefix(const char *p, const char *prefix) {
 const size_t n = strlen(prefix);
 return strncmp(p, prefix, n) == 0;
}
static inline bool cmp5_direct_child(const char *p, const char *prefix) {
 if (!cmp5_has_prefix(p, prefix)) return false;
 return cmp5_safe_component(p + strlen(prefix));
}
static inline bool cmp5_image_suffix(const char *name) {
 const char *dot = strrchr(name, '.');
 return dot && (!strcasecmp(dot, ".exfat") || !strcasecmp(dot, ".ffpkg") ||
                !strcasecmp(dot, ".ffpfs") || !strcasecmp(dot, ".ffpfsc"));
}
static inline bool cmp5_game_source_allowed(const char *path) {
 if (!path || path[0] != '/' || strlen(path) >= 192) return false;
 if (cmp5_direct_child(path, "/data/homebrew/") ||
     cmp5_direct_child(path, "/data/etaHEN/games/")) return true;
 if (!cmp5_has_prefix(path, "/mnt/")) return false;
 const char *p = path + strlen("/mnt/");
 if (!cmp5_has_prefix(p, "usb") && !cmp5_has_prefix(p, "ext")) return false;
 p += 3;
 if (*p < '0' || *p > '7' || p[1] != '/') return false;
 p += 2;
 if (cmp5_direct_child(p, "homebrew/") ||
     cmp5_direct_child(p, "etaHEN/games/")) return true;
 return cmp5_safe_component(p) && cmp5_image_suffix(p);
}
static inline bool cmp5_game_destination_allowed(const char *path) {
 if (!path) return false;
 if (strcmp(path, "/data/homebrew") == 0) return true;
 if (!cmp5_has_prefix(path, "/mnt/")) return false;
 const char *p = path + strlen("/mnt/");
 if (!cmp5_has_prefix(p, "usb") && !cmp5_has_prefix(p, "ext")) return false;
 p += 3;
 return p[0] >= '0' && p[0] <= '7' &&
        strcmp(p + 1, "/homebrew") == 0;
}
#endif
