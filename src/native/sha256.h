#ifndef CMP5_SHA256_H
#define CMP5_SHA256_H
#include <stddef.h>
#include <stdint.h>
typedef struct {uint32_t h[8];uint8_t block[64];uint64_t total;size_t used;} Cmp5Sha256;
void cmp5_sha256_init(Cmp5Sha256*ctx);
void cmp5_sha256_update(Cmp5Sha256*ctx,const void*data,size_t len);
void cmp5_sha256_final(Cmp5Sha256*ctx,uint8_t out[32]);
#endif
