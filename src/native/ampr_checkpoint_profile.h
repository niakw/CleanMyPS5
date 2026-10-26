/* CleanMyPS5: immutable profile-content pin in AMPR's 63-byte
 * reserved request.bin area (E2AREQ05 on the pinned x86-64 ABI).
 * This does not authenticate request.bin; it only prevents silent plan
 * changes from a different profile when resuming a partial conversion. */
#ifndef CMP5_AMPR_PROFILE_PIN_H
#define CMP5_AMPR_PROFILE_PIN_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define CMP5_PROFILE_PIN_BYTES 63u
static inline int cmp5_profile_hex_digit(char c){
 if(c>='0'&&c<='9')return c-'0';
 if(c>='a'&&c<='f')return c-'a'+10;
 if(c>='A'&&c<='F')return c-'A'+10;
 return -1;
}
static inline bool cmp5_profile_pin_set(uint8_t reserved[CMP5_PROFILE_PIN_BYTES],
                                        const char*sha256_hex){
 if(!reserved)return false;
 uint8_t proposed[CMP5_PROFILE_PIN_BYTES]={0};
 memcpy(proposed,"CMP5PF01",8);
 if(sha256_hex){
  /* Digest always comes from e2a_sha256_file(char hex[65]). Never read
   * unbounded peer input or store a truncated digest. */
  for(size_t i=0;i<64u;i++)if(!sha256_hex[i])return false;
  if(sha256_hex[64]!=0)return false;
  proposed[8]=1u;
  for(size_t i=0;i<32u;i++){
   int hi=cmp5_profile_hex_digit(sha256_hex[i*2u]);
   int lo=cmp5_profile_hex_digit(sha256_hex[i*2u+1u]);
   if(hi<0||lo<0)return false;
   proposed[9u+i]=(uint8_t)((hi<<4)|lo);
  }
 }
 memcpy(reserved,proposed,sizeof(proposed));
 return true;
}
static inline bool cmp5_profile_pin_valid(const uint8_t reserved[CMP5_PROFILE_PIN_BYTES]){
 if(!reserved||memcmp(reserved,"CMP5PF01",8)!=0||
    reserved[8]>1u)return false;
 /* An optional second pin records the immutable file-action plan
  * produced AFTER profile rules, synthetic runtime and block budgeting.
  * P2 + first 160 bits of SHA-256 occupy the remaining 22 bytes. */
 if(reserved[41]==0u){
  for(size_t i=42u;i<CMP5_PROFILE_PIN_BYTES;i++)
   if(reserved[i]!=0u)return false;
 }else{
  if(reserved[41]!='P'||reserved[42]!='2')return false;
 }
 if(!reserved[8])
  for(size_t i=9u;i<41u;i++)
   if(reserved[i]!=0u)return false;
 return true;
}
static inline bool cmp5_profile_pin_matches(
 const uint8_t saved[CMP5_PROFILE_PIN_BYTES],const char*sha256_hex){
 if(!cmp5_profile_pin_valid(saved))return false;
 uint8_t proposed[CMP5_PROFILE_PIN_BYTES]={0};
 /* The separate file-plan digest lives in bytes 41..62 and must not
  * invalidate a matching profile fingerprint during the early preflight. */
 return cmp5_profile_pin_set(proposed,sha256_hex)&&
        memcmp(saved,proposed,41u)==0;
}
/* Save/compare an ordered, already finalized AMPR file-action plan.
 * A legacy checkpoint without this pin cannot be resumed safely because
 * a changed source file set could reuse its existing LZ4 volumes. */
static inline bool cmp5_plan_pin_valid(
 const uint8_t reserved[CMP5_PROFILE_PIN_BYTES]){
 return reserved&&cmp5_profile_pin_valid(reserved)&&
        reserved[41]=='P'&&reserved[42]=='2';
}
static inline bool cmp5_plan_pin_set(
 uint8_t reserved[CMP5_PROFILE_PIN_BYTES],const char*sha256_hex){
 if(!reserved||!sha256_hex||!cmp5_profile_pin_valid(reserved))return false;
 uint8_t proposed[20]={0};
 for(size_t i=0;i<64u;i++)if(!sha256_hex[i])return false;
 if(sha256_hex[64]!=0)return false;
 for(size_t i=0;i<20u;i++){
  int hi=cmp5_profile_hex_digit(sha256_hex[i*2u]);
  int lo=cmp5_profile_hex_digit(sha256_hex[i*2u+1u]);
  if(hi<0||lo<0)return false;
  proposed[i]=(uint8_t)((hi<<4)|lo);
 }
 for(size_t i=40u;i<64u;i++)
  if(cmp5_profile_hex_digit(sha256_hex[i])<0)return false;
 reserved[41]='P';reserved[42]='2';
 memcpy(reserved+43,proposed,sizeof(proposed));
 return true;
}
static inline bool cmp5_plan_pin_matches(
 const uint8_t reserved[CMP5_PROFILE_PIN_BYTES],const char*sha256_hex){
 if(!cmp5_plan_pin_valid(reserved))return false;
 uint8_t proposed[CMP5_PROFILE_PIN_BYTES];
 memcpy(proposed,reserved,sizeof(proposed));
 return cmp5_plan_pin_set(proposed,sha256_hex)&&
        memcmp(reserved,proposed,sizeof(proposed))==0;
}
#endif
