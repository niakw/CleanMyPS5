/* CleanMyPS5: only scan-listed direct game entries and known writable
 * destination roots are accepted by the embedded AMPR job interface.
 * No follow-up path resolution, no permission elevation.
 * GPL-3.0-or-later. Copyright (C) 2026 CleanMyPS5 contributors. */
#ifndef CMP5_COMPRESSION_ALLOWED_PATHS_H
#define CMP5_COMPRESSION_ALLOWED_PATHS_H
#include <stdbool.h>
#include <stddef.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>

static inline bool cmp5_safe_component(const char *name) {
 if (!name || !*name || name[0] == '.') return false;
 const size_t n = strlen(name);
 if (n == 0 || n >= 64) return false;
 for (size_t i = 0; i < n; ++i) {
  const unsigned char c = (unsigned char)name[i];
  if (!((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '_' || c == '-' || c == '.' || c == ' ' ||
        c == '[' || c == ']' || c == '(' || c == ')'))
   return false;
 }
 return true;
}
static inline bool cmp5_has_prefix(const char *p, const char *prefix) {
 const size_t n = strlen(prefix);
 return strncmp(p, prefix, n) == 0;
}
static inline bool cmp5_direct_child(const char *p, const char *prefix) {
 if (!cmp5_has_prefix(p, prefix)) return false;
 return cmp5_safe_component(p + strlen(prefix));
}
static inline bool cmp5_image_suffix(const char *name) {
 const char *dot = strrchr(name, '.');
 return dot && (!strcasecmp(dot, ".exfat") || !strcasecmp(dot, ".ffpkg") ||
                !strcasecmp(dot, ".ffpfs") || !strcasecmp(dot, ".ffpfsc"));
}
static inline bool cmp5_game_source_allowed(const char *path) {
 if (!path || path[0] != '/' || strlen(path) >= 192) return false;
 if (cmp5_direct_child(path, "/data/homebrew/") ||
     cmp5_direct_child(path, "/data/etaHEN/games/")) return true;
 if (!cmp5_has_prefix(path, "/mnt/")) return false;
 const char *p = path + strlen("/mnt/");
 if (!cmp5_has_prefix(p, "usb") && !cmp5_has_prefix(p, "ext")) return false;
 p += 3;
 if (*p < '0' || *p > '7' || p[1] != '/') return false;
 p += 2;
 if (cmp5_direct_child(p, "homebrew/") ||
     cmp5_direct_child(p, "etaHEN/games/")) return true;
 return cmp5_safe_component(p) && cmp5_image_suffix(p);
}
static inline bool cmp5_game_destination_allowed(const char *path) {
 if (!path) return false;
 if (strcmp(path, "/data/homebrew") == 0) return true;
 if (!cmp5_has_prefix(path, "/mnt/")) return false;
 const char *p = path + strlen("/mnt/");
 if (!cmp5_has_prefix(p, "usb") && !cmp5_has_prefix(p, "ext")) return false;
 p += 3;
 return p[0] >= '0' && p[0] <= '7' &&
        strcmp(p + 1, "/homebrew") == 0;
}

/* Resolve every path component without following symlinks. Lexical path
 * allowlists are insufficient when an allowed parent (e.g. a homebrew
 * folder or /mnt/usbN) is replaced by a symlink to protected user data.
 * This is a read-only admission precheck, not a lock on later packer IO.
 * 'root' is "/" in the actual engine and a fake console tree in tests.
 */
static inline bool cmp5_game_path_on_disk(const char *root,const char *path,
                                         bool destination) {
 if (!root || !path || !*root || path[0]!='/' ||
     !(destination?cmp5_game_destination_allowed(path):
                   cmp5_game_source_allowed(path))) return false;
 int dir=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(dir<0)return false;
 const char*cur=path+1;
 bool ok=false;
 for(;;){
  const char*slash=strchr(cur,'/');
  const size_t len=slash?(size_t)(slash-cur):strlen(cur);
  char segment[65];
  if(len==0||len>=sizeof(segment))break;
  memcpy(segment,cur,len);
  segment[len]=0;
  const int child=openat(dir,segment,O_RDONLY|O_NOFOLLOW|
    (slash?O_DIRECTORY:O_NONBLOCK));
  if(child<0)break;
  struct stat meta;
  const int success=fstat(child,&meta)==0;
  if(!slash){
   ok=success&&
      (destination?S_ISDIR(meta.st_mode):
        (S_ISDIR(meta.st_mode)||
         (S_ISREG(meta.st_mode)&&meta.st_nlink==1&&meta.st_size>0)));
   close(child);
   break;
  }
  if(!success||!S_ISDIR(meta.st_mode)){close(child);break;}
  close(dir);dir=child;
  cur=slash+1;
 }
 close(dir);
 return ok;
}
#endif
