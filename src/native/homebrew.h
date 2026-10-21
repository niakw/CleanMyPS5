#ifndef CMP5_HOMEBREW_H
#define CMP5_HOMEBREW_H
#include <stddef.h>
/* Read-only bounded inventory of known homebrew and plugin directories. Caller frees output. */
char *cmp5_homebrew(const char *root, size_t *length);
#endif
