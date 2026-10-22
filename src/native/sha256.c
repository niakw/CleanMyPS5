#include "sha256.h"
#include <string.h>
static uint32_t ror(uint32_t a,unsigned n){return (a>>n)|(a<<(32u-n));}
static const uint32_t k[64]={
0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
static uint32_t get32(const uint8_t*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put32(uint8_t*p,uint32_t x){p[0]=(uint8_t)(x>>24);p[1]=(uint8_t)(x>>16);p[2]=(uint8_t)(x>>8);p[3]=(uint8_t)x;}
static void transform(Cmp5Sha256*c){
 uint32_t w[64];for(unsigned i=0;i<16;i++)w[i]=get32(c->block+4*i);
 for(unsigned i=16;i<64;i++){uint32_t a=w[i-15],b=w[i-2];w[i]=(ror(b,17)^ror(b,19)^(b>>10))+w[i-7]+(ror(a,7)^ror(a,18)^(a>>3))+w[i-16];}
 uint32_t a=c->h[0],b=c->h[1],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],h=c->h[7],q=c->h[2];
 for(unsigned i=0;i<64;i++){uint32_t t1=h+(ror(e,6)^ror(e,11)^ror(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t t2=(ror(a,2)^ror(a,13)^ror(a,22))+((a&b)^(a&q)^(b&q));h=g;g=f;f=e;e=d+t1;d=q;q=b;b=a;a=t1+t2;}
 c->h[0]+=a;c->h[1]+=b;c->h[2]+=q;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
void cmp5_sha256_init(Cmp5Sha256*c){static const uint32_t initial[]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};memcpy(c->h,initial,sizeof(initial));c->used=0;c->total=0;}
void cmp5_sha256_update(Cmp5Sha256*c,const void*input,size_t len){const uint8_t*p=(const uint8_t*)input;c->total+=len;while(len){size_t n=64-c->used;if(n>len)n=len;memcpy(c->block+c->used,p,n);c->used+=n;p+=n;len-=n;if(c->used==64){transform(c);c->used=0;}}}
void cmp5_sha256_final(Cmp5Sha256*c,uint8_t out[32]){uint64_t bits=c->total*8u;c->block[c->used++]=0x80u;if(c->used>56){memset(c->block+c->used,0,64-c->used);transform(c);c->used=0;}memset(c->block+c->used,0,56-c->used);for(unsigned i=0;i<8;i++)c->block[56+i]=(uint8_t)(bits>>(56-8*i));transform(c);for(unsigned i=0;i<8;i++)put32(out+4*i,c->h[i]);memset(c,0,sizeof(*c));}
