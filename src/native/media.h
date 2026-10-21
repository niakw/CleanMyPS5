#ifndef CMP5_MEDIA_H
#define CMP5_MEDIA_H
#include <stddef.h>
/* Resolves ONLY original JPEG/PNG captures under the read-only photo tree.
   The path is absolute relative to the console root, not an arbitrary OS path.
   Successful caller owns fd and must close it. No write operations exist. */
int cmp5_open_preview(const char *root, const char *path, int *fd,
                      size_t *bytes, const char **mime);
#endif
