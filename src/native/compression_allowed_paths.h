/* CleanMyPS5: only scan-listed direct game entries and known writable
 * destination roots are accepted by the embedded AMPR job interface.
 * No follow-up path resolution, no permission elevation.
 * GPL-3.0-or-later. Copyright (C) 2026 CleanMyPS5 contributors. */
#ifndef CMP5_COMPRESSION_ALLOWED_PATHS_H
#define CMP5_COMPRESSION_ALLOWED_PATHS_H
#include <stdbool.h>
#include <errno.h>
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
/* An image extension on a DIRECTORY can name an extracted game dump.
 * Only a regular image FILE must be linked to ShadowMountPlus.
 * The caller must obtain metadata via the descriptor-anchored path guard.
 */
static inline bool cmp5_source_requires_smp(const char *path,
                                            const struct stat *observed) {
 return path && observed && S_ISREG(observed->st_mode) &&
        cmp5_image_suffix(path);
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
/* Obtain a snapshot only from an explicitly allowed root, using an
 * O_NOFOLLOW descriptor for EVERY component, not a pathname-based stat. */
static inline bool cmp5_game_path_stat(const char*root,const char*path,
                                      bool destination,struct stat*out) {
 if(out)memset(out,0,sizeof(*out));
 if(!root||!path||!out||!*root||path[0]!='/'||
    !(destination?cmp5_game_destination_allowed(path):
                   cmp5_game_source_allowed(path)))return false;
 int dir=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(dir<0)return false;
 const char*cur=path+1;
 bool ok=false;
 for(;;){
  const char*slash=strchr(cur,'/');
  const size_t len=slash?(size_t)(slash-cur):strlen(cur);
  char segment[65];
  if(len==0||len>=sizeof(segment))break;
  memcpy(segment,cur,len);segment[len]=0;
  int child=openat(dir,segment,O_RDONLY|O_NOFOLLOW|
    (slash?O_DIRECTORY:O_NONBLOCK));
  if(child<0)break;
  struct stat meta={0};
  const int success=fstat(child,&meta)==0;
  if(!slash){
   ok=success&&
      (destination?S_ISDIR(meta.st_mode):
         (S_ISDIR(meta.st_mode)||
         (S_ISREG(meta.st_mode)&&meta.st_nlink==1&&meta.st_size>0)));
   if(ok)*out=meta;
   close(child);
   break;
  }
  if(!success||!S_ISDIR(meta.st_mode)){close(child);break;}
  close(dir);dir=child;cur=slash+1;
 }
 close(dir);
 return ok;
}
static inline bool cmp5_game_path_on_disk(const char*root,const char*path,
                                          bool destination) {
 struct stat snapshot={0};
 return cmp5_game_path_stat(root,path,destination,&snapshot);
}
/* Compare complete POSIX identity with subsecond timestamp precision.
 * This detects replacements by another REGULAR file or directory while
 * SMP is calculating compatibility. It does not lock the later worker. */
static inline bool cmp5_game_same_stat(const struct stat*a,
                                       const struct stat*b){
 if(!a||!b)return false;
#if defined(__APPLE__)
 const bool nanos=a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec&&
                  a->st_ctimespec.tv_nsec==b->st_ctimespec.tv_nsec;
#else
 const bool nanos=a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
                  a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
#endif
 return nanos&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
   a->st_mode==b->st_mode&&a->st_nlink==b->st_nlink&&
   a->st_size==b->st_size&&a->st_mtime==b->st_mtime&&
   a->st_ctime==b->st_ctime;
}
static inline bool cmp5_game_path_still_same(const char*root,
                                             const char*path,bool destination,
                                             const struct stat*before) {
 struct stat current={0};
 return cmp5_game_path_stat(root,path,destination,&current)&&
        cmp5_game_same_stat(before,&current);
}

/* SMP-managed image output must be an entirely NEW title directory.
 * A conflicting file or even dangling symlink blocks the job; no write.
 * This check is not a substitute for atomic creation by the worker. */
static inline bool cmp5_game_output_vacant(const char*root,const char*dest,
                                           const char*title_id) {
 if(!root||!*root||!dest||!title_id||
    !cmp5_game_destination_allowed(dest)||strlen(title_id)!=9)return false;
 for(size_t i=0;i<9;i++){
  const char c=title_id[i];
  if(i<4?(c<'A'||c>'Z'):(c<'0'||c>'9'))return false;
 }
 int dir=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(dir<0)return false;
 for(const char*cur=dest+1;*cur;){
  const char*slash=strchr(cur,'/');
  const size_t n=slash?(size_t)(slash-cur):strlen(cur);
  char name[65];
  if(!n||n>=sizeof(name)){close(dir);return false;}
  memcpy(name,cur,n);name[n]=0;
  int child=openat(dir,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  close(dir);
  if(child<0)return false;
  dir=child;
  if(!slash)break;
  cur=slash+1;
 }
 struct stat existing={0};
 errno=0;
 const int found=fstatat(dir,title_id,&existing,AT_SYMLINK_NOFOLLOW);
 const int error=errno;
 close(dir);
 return found<0&&error==ENOENT;
}
/* Resume reads persisted job options: first ensure this is an allowed,
 * direct Title ID output directory with no symlinks or nested mounts. */
static inline bool cmp5_game_resume_path_stat(const char*root,
                                              const char*path,
                                              struct stat*out){
 if(out)memset(out,0,sizeof(*out));
 if(!root||!path||!out||strlen(path)>=192)return false;
 const char*slash=strrchr(path,'/');
 if(!slash||slash==path||strlen(slash+1)!=9)return false;
 for(size_t i=0;i<9;i++){
  const char c=slash[1+i];
  if(i<4?(c<'A'||c>'Z'):(c<'0'||c>'9'))return false;
 }
 char parent[192];
 const size_t n=(size_t)(slash-path);
 if(!n||n>=sizeof(parent))return false;
 memcpy(parent,path,n);parent[n]=0;
 struct stat root_stat={0};
 return cmp5_game_path_stat(root,parent,true,&root_stat)&&
   cmp5_game_path_stat(root,path,false,out)&&
   S_ISDIR(out->st_mode)&&root_stat.st_dev==out->st_dev;
}
static inline bool cmp5_game_resume_path_still_same(const char*root,
                                                     const char*path,
                                                     const struct stat*before){
 struct stat current={0};
 return before&&cmp5_game_resume_path_stat(root,path,&current)&&
        cmp5_game_same_stat(before,&current);
}
#endif
