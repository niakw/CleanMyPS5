/* Read-only image scanner: directory identity and timestamp precision.
 * A changed directory must not contribute a supposedly complete game size.
 * Observations cannot replace a later conversion-specific preflight.
 * GPL-3.0-or-later. */
#ifndef CMP5_GAME_SCAN_IDENTITY_H
#define CMP5_GAME_SCAN_IDENTITY_H
#include <sys/stat.h>

static inline int cmp5_same_game_directory(const struct stat *a,
                                            const struct stat *b) {
 if (!a || !b || !S_ISDIR(a->st_mode) || !S_ISDIR(b->st_mode)) return 0;
#if defined(__APPLE__)
 const int fine = a->st_mtimespec.tv_nsec == b->st_mtimespec.tv_nsec &&
                  a->st_ctimespec.tv_nsec == b->st_ctimespec.tv_nsec;
#else
 const int fine = a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
                  a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
#endif
 return fine && a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
        a->st_mode == b->st_mode && a->st_nlink == b->st_nlink &&
        a->st_size == b->st_size && a->st_mtime == b->st_mtime &&
        a->st_ctime == b->st_ctime;
}
#endif
