#ifndef CMP5_INVENTORY_H
#define CMP5_INVENTORY_H
#include <stddef.h>
/* JSON report; read-only. Caller owns returned buffer and must free(). */
char *cmp5_report(const char *root, size_t *len);
#endif
