#ifndef CMP5_VOLUMES_H
#define CMP5_VOLUMES_H
#include <stddef.h>
// Enumerates fixed, non-symlink PS5 mount roots and reports statvfs data.
// Different named paths may still refer to the SAME underlying filesystem.
char *cmp5_volumes(const char *root, size_t *bytes);
#endif
