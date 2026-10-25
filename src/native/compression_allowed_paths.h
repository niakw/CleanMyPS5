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

/* Reject unterminated fixed-array metadata from ShadowMountPlus.
 * Never scan past a structure field or accept truncated game identities. */
static inline bool cmp5_game_smp_field_eq(const char *field,size_t capacity,
                                         const char *expected) {
 if(!field||!expected||!capacity)return false;
 const char *end=(const char *)memchr(field,0,capacity);
 if(!end||end==field)return false;
 const size_t n=(size_t)(end-field);
 /* The peer field can itself come from an untrusted saved checkpoint.
  * The caller's known fixed-width SMP capacity is also an upper bound
  * on any string that could legitimately match it. */
 const char *expected_end=(const char *)memchr(expected,0,capacity);
 return expected_end&&(size_t)(expected_end-expected)==n&&
        memcmp(field,expected,n)==0;
}
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
static inline bool cmp5_convertible_image_suffix(const char *name) {
 if(!name)return false;
 const char *dot=strrchr(name,'.');
 return dot&&(!strcasecmp(dot,".exfat")||!strcasecmp(dot,".ffpkg")||
             !strcasecmp(dot,".ffpfs"));
}
/* .ffpfsc is recognized by the scanner as an image but already compressed:
 * it is not eligible for another AMPR conversion. */
static inline bool cmp5_image_suffix(const char *name) {
 const char *dot=name?strrchr(name,'.'):NULL;
 return cmp5_convertible_image_suffix(name) ||
        (dot&&!strcasecmp(dot,".ffpfsc"));
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
         (S_ISREG(meta.st_mode)&&meta.st_nlink==1&&meta.st_size>0&&
          cmp5_convertible_image_suffix(path))));
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
/* Reserve a NEW title output and private checkpoint directory atomically
 * relative to descriptor-anchored, O_NOFOLLOW storage directories.
 * A plain mkdir("/mnt/usbN/homebrew/TITLE") can follow a replaced
 * homebrew parent. Never remove output after errors; partial output is
 * deliberately retained for an explicit audit or resume.
 */
static inline bool cmp5_game_output_create_unique(
 const char*root,const char*dest,const char*title_id,struct stat*out){
 if(out)memset(out,0,sizeof(*out));
 if(!root||!*root||!dest||!title_id||!out||
    !cmp5_game_destination_allowed(dest)||strlen(title_id)!=9)return false;
 for(unsigned i=0;i<9;i++){
  const char c=title_id[i];
  if(i<4?(c<'A'||c>'Z'):(c<'0'||c>'9'))return false;
 }
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return false;
 for(const char*cur=dest+1;*cur;){
  const char*slash=strchr(cur,'/');
  const size_t n=slash?(size_t)(slash-cur):strlen(cur);
  char part[65];
  if(!n||n>=sizeof(part)){close(fd);return false;}
  memcpy(part,cur,n);part[n]=0;
  const int child=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  close(fd);
  if(child<0)return false;
  fd=child;
  if(!slash)break;
  cur=slash+1;
 }
 struct stat parent={0};
 if(fstat(fd,&parent)!=0||!S_ISDIR(parent.st_mode)){
  close(fd);return false;
 }
 /* mkdirat never reuses an existing file, directory or dangling link. */
 if(mkdirat(fd,title_id,0777)!=0){close(fd);return false;}
 struct stat held={0},named={0};
 const int child=openat(fd,title_id,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 bool ok=child>=0 &&
   mkdirat(child,".e2a-state",0777)==0 &&
   fstat(child,&held)==0&&
   fstatat(fd,title_id,&named,AT_SYMLINK_NOFOLLOW)==0&&
   S_ISDIR(held.st_mode)&&S_ISDIR(named.st_mode)&&
   held.st_dev==parent.st_dev&&held.st_dev==named.st_dev&&
   held.st_ino==named.st_ino;
 if(child>=0)close(child);
 close(fd);
 if(!ok)return false; /* leave uncommitted output for inspection */
 *out=held;
 return true;
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
/* Checkpoint directories are NOT arbitrary file paths. Traverse the
 * allowlisted title output by O_NOFOLLOW handles, then open the private
 * .e2a-state child without following aliases. A failed read on PS5 must
 * not silently restart a partial job at the "pack" stage. */
static inline int cmp5_game_open_checkpoint_dir(const char*root,
                                                const char*state_dir){
 static const char suffix[]="/.e2a-state";
 if(!root||!state_dir)return -1;
 const size_t n=strlen(state_dir),tail=sizeof(suffix)-1u;
 if(n<=tail||n>=192u||strcmp(state_dir+n-tail,suffix)!=0)return -1;
 char title[192];
 memcpy(title,state_dir,n-tail);title[n-tail]=0;
 struct stat title_snapshot={0};
 if(!cmp5_game_resume_path_stat(root,title,&title_snapshot))return -1;
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return -1;
 for(const char*cur=title+1;*cur;){
  const char*slash=strchr(cur,'/');
  const size_t len=slash?(size_t)(slash-cur):strlen(cur);
  char part[65];
  if(!len||len>=sizeof(part)){close(fd);return -1;}
  memcpy(part,cur,len);part[len]=0;
  int next=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  close(fd);
  if(next<0)return -1;
  fd=next;
  if(!slash)break;
  cur=slash+1;
 }
 struct stat parent={0},named={0},opened={0};
 int child=openat(fd,".e2a-state",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 const bool ok=child>=0&&fstat(fd,&parent)==0&&
   fstat(child,&opened)==0&&
   fstatat(fd,".e2a-state",&named,AT_SYMLINK_NOFOLLOW)==0&&
   S_ISDIR(parent.st_mode)&&S_ISDIR(named.st_mode)&&
   S_ISDIR(opened.st_mode)&&
   parent.st_dev==title_snapshot.st_dev&&
   parent.st_ino==title_snapshot.st_ino&&
   opened.st_dev==named.st_dev&&opened.st_ino==named.st_ino&&
   cmp5_game_resume_path_still_same(root,title,&title_snapshot);
 close(fd);
 if(!ok){if(child>=0)close(child);return -1;}
 return child;
}
#endif
