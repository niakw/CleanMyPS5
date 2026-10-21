#ifndef CMP5_PREFLIGHT_H
#define CMP5_PREFLIGHT_H
#include <stddef.h>
/* Checks only fixed, documented directories. No user files are opened/read. */
char *cmp5_preflight(const char *root, size_t *out_len);
#endif
