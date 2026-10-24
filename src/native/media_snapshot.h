/* Shared read-only candidate identity: preserve mtime and ctime nanoseconds. */
#ifndef CMP5_MEDIA_SNAPSHOT_H
#define CMP5_MEDIA_SNAPSHOT_H
#include "inventory.h"
#include <sys/stat.h>
static inline uint32_t cmp5_mtime_ns(const struct stat*s){
#if defined(__APPLE__)
 return (uint32_t)s->st_mtimespec.tv_nsec;
#else
 return (uint32_t)s->st_mtim.tv_nsec;
#endif
}
static inline uint32_t cmp5_ctime_ns(const struct stat*s){
#if defined(__APPLE__)
 return (uint32_t)s->st_ctimespec.tv_nsec;
#else
 return (uint32_t)s->st_ctim.tv_nsec;
#endif
}
static inline void cmp5_snapshot_capture(Cmp5MediaCandidate*c,const struct stat*s){
 c->modified_at=s->st_mtime>0?(int64_t)s->st_mtime:0;
 c->snapshot_device=(uint64_t)s->st_dev;
 c->snapshot_inode=(uint64_t)s->st_ino;
 c->snapshot_ctime=(int64_t)s->st_ctime;
 c->snapshot_mtime_nsec=cmp5_mtime_ns(s);
 c->snapshot_ctime_nsec=cmp5_ctime_ns(s);
}
static inline int cmp5_snapshot_matches(const Cmp5MediaCandidate*c,const struct stat*s){
 return c&&s&&S_ISREG(s->st_mode)&&s->st_nlink==1&&s->st_size>0&&
  (uint64_t)s->st_size==c->bytes&&
  (uint64_t)s->st_dev==c->snapshot_device&&
  (uint64_t)s->st_ino==c->snapshot_inode&&
  (s->st_mtime>0?(int64_t)s->st_mtime:0)==c->modified_at&&
  (int64_t)s->st_ctime==c->snapshot_ctime&&
  cmp5_mtime_ns(s)==c->snapshot_mtime_nsec&&
  cmp5_ctime_ns(s)==c->snapshot_ctime_nsec;
}
static inline int cmp5_snapshot_same(const Cmp5MediaCandidate*a,const Cmp5MediaCandidate*b){
 return a&&b&&a->bytes==b->bytes&&a->modified_at==b->modified_at&&
  a->snapshot_device==b->snapshot_device&&a->snapshot_inode==b->snapshot_inode&&
  a->snapshot_ctime==b->snapshot_ctime&&
  a->snapshot_mtime_nsec==b->snapshot_mtime_nsec&&
  a->snapshot_ctime_nsec==b->snapshot_ctime_nsec;
}
#endif
