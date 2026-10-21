#ifndef CMP5_DUPLICATES_H
#define CMP5_DUPLICATES_H
#include <stddef.h>
/* Exact, bounded, read-only screenshot comparison. Caller frees JSON. */
char *cmp5_duplicates(const char *root, size_t *length);
#endif
