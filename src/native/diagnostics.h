#ifndef CMP5_DIAGNOSTICS_H
#define CMP5_DIAGNOSTICS_H
#include <stddef.h>
/* Read-only summary of known etaHEN/autoloader config paths; caller frees JSON. */
char *cmp5_diagnostics(const char *namespace_root, size_t *length);
#endif
