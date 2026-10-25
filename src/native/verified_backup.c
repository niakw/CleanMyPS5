#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "verified_backup.h"
#include "sha256.h"
#include "private_workspace_guard.h"
#include "media_snapshot.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define HEADER_SIZE 64u
#define BUFFER_SIZE 65536u
static const uint8_t magic[8]={'C','M','P','5','B','A','K','1'};
static int canceled(Cmp5CancelCheck f,void*ctx){return f&&f(ctx);}
static int valid_id(const char*id){
 if(!id)return 0;
 size_t n=0;
 for(;id[n]&&n<48;n++){
  const char c=id[n];
  if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return 0;
 }
 return n>=1&&n<=48&&id[n]==0;
}
static int private_dir(const char*path){
 return cmp5_open_private_workspace(path);
}
static int same(const struct stat*a,const struct stat*b){
#if defined(__APPLE__)
 const int precise=a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec &&
  a->st_ctimespec.tv_nsec==b->st_ctimespec.tv_nsec;
#else
 const int precise=a->st_mtim.tv_nsec==b->st_mtim.tv_nsec &&
  a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
#endif
 return precise&&S_ISREG(a->st_mode)&&S_ISREG(b->st_mode)&&
  a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
  a->st_size==b->st_size&&a->st_mtime==b->st_mtime&&
  a->st_ctime==b->st_ctime;
}
/* A valid opened descriptor can outlive a rename of its directory.
 * Recheck that the selected private workspace still names the same
 * directory before returning a verified capsule receipt. */
static int same_private_directory(const struct stat*a,const struct stat*b){
#if defined(__APPLE__)
 const int nanos=a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec&&
                 a->st_ctimespec.tv_nsec==b->st_ctimespec.tv_nsec;
#else
 const int nanos=a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
                 a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
#endif
 return nanos&&S_ISDIR(a->st_mode)&&S_ISDIR(b->st_mode)&&
   a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
   a->st_mode==b->st_mode&&a->st_nlink==b->st_nlink&&
   a->st_size==b->st_size&&a->st_mtime==b->st_mtime&&
   a->st_ctime==b->st_ctime;
}
static int private_directory_current(int dir,const char*path,
                                     const struct stat*initial){
 struct stat held,current;
 if(fstat(dir,&held)!=0||!same_private_directory(initial,&held))return 0;
 int reopened=private_dir(path);
 if(reopened<0)return 0;
 const int ok=fstat(reopened,&current)==0&&
   same_private_directory(initial,&current);
 close(reopened);
 return ok;
}
/* A restore writes to the destination directory, so its mtime/ctime
 * legitimately change. Check stable inode and private permissions here;
 * the stricter metadata-equality check above remains for read-only verify. */
static int private_directory_identity_current(int dir,const char*path,
 const struct stat*original){
 struct stat anchored={0},named={0};
 if(fstat(dir,&anchored)!=0||!S_ISDIR(anchored.st_mode)||
    anchored.st_dev!=original->st_dev||
    anchored.st_ino!=original->st_ino||
    anchored.st_uid!=original->st_uid||
    anchored.st_mode!=original->st_mode||
    anchored.st_nlink!=original->st_nlink)return 0;
 int reopened=private_dir(path);
 if(reopened<0)return 0;
 const int ok=fstat(reopened,&named)==0&&
   anchored.st_dev==named.st_dev&&anchored.st_ino==named.st_ino&&
   anchored.st_uid==named.st_uid&&anchored.st_mode==named.st_mode&&
   anchored.st_nlink==named.st_nlink;
 close(reopened);
 return ok;
}
static int snapshot_matches(const struct stat*s,const Cmp5MediaCandidate*c){
 return cmp5_snapshot_matches(c,s);
}
static int source_file(const char*root,const Cmp5MediaCandidate*c,struct stat*out){
 if(!root||!c||!out||c->bytes==0||c->bytes>CMP5_BACKUP_LIMIT)return -1;
 const char*prefix=c->media_type==1?"/user/av_contents/photo/":
  c->media_type==2?"/user/av_contents/video/":
  c->media_type==4?"/user/av_contents/thumbnails/photo/":
  c->media_type==3?
   (!strncmp(c->preview_path,"/user/av_contents/photo/",24)?
    "/user/av_contents/photo/":"/user/av_contents/video/"):NULL;
 if(!prefix)return -1;
 const char*path=c->preview_path;
 size_t n=0;while(n<sizeof(c->preview_path)&&path[n])n++;
 if(n>=sizeof(c->preview_path)||n<=strlen(prefix)||
    memcmp(path,prefix,strlen(prefix))!=0)return -1;
 const char *ext=strrchr(path,'.');
 if(!ext)return -1;
 if(c->media_type==1){
  if(strcasecmp(ext,".jpg")&&strcasecmp(ext,".jpeg")&&
     strcasecmp(ext,".png")&&strcasecmp(ext,".jxr"))return -1;
 }else if(c->media_type==2){
  if(strcasecmp(ext,".mp4")&&strcasecmp(ext,".webm"))return -1;
 }else if(c->media_type==4){
  /* .jpg.jpeg is one compound suffix; the generic last-dot pointer
   * must not classify arbitrary *.jpeg files as a Gallery thumbnail. */
  if(n<9u||strcasecmp(path+n-9u,".jpg.jpeg")||
     c->bytes>64u*1024u*1024u)return -1;
 }else if(strcasecmp(ext,".dat")||c->bytes>65536u)return -1;
 for(size_t i=0;i<n;i++)if(path[i]=='\\'||(i&&path[i]=='/'&&path[i-1]=='/'))return -1;
 if(path[n-1]=='/')return -1;
 char segments[sizeof(c->preview_path)];
 memcpy(segments,path+1,n);segments[n-1]=0;
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(fd<0)return -1;
 char*save=NULL;
 for(char*part=strtok_r(segments,"/",&save);part;){
  if(part[0]=='.'){close(fd);return -1;}
  char*next=strtok_r(NULL,"/",&save);
  int child=openat(fd,part,O_RDONLY|O_NOFOLLOW|
   (next?O_DIRECTORY:O_NONBLOCK));
  close(fd);
  if(child<0)return -1;
  fd=child;part=next;
 }
 if(fstat(fd,out)!=0||!snapshot_matches(out,c)){close(fd);return -1;}
 return fd;
}
static void put64(uint8_t*p,uint64_t value){for(unsigned i=0;i<8;i++)p[7-i]=(uint8_t)(value>>(i*8));}
static uint64_t get64(const uint8_t*p){uint64_t x=0;for(unsigned i=0;i<8;i++)x=(x<<8)|p[i];return x;}
static int full_write(int fd,const uint8_t*buf,size_t size){
 while(size){ssize_t n=write(fd,buf,size);if(n<0&&errno==EINTR)continue;
  if(n<=0)return 0;
  size-=(size_t)n;buf+=n;}
 return 1;
}
static int full_read(int fd,uint8_t*buf,size_t size){
 while(size){ssize_t n=read(fd,buf,size);if(n<0&&errno==EINTR)continue;
  if(n<=0)return 0;
  size-=(size_t)n;buf+=n;}
 return 1;
}
static int copy_body(int source,int dest,uint64_t bytes,uint8_t digest[32],
 Cmp5CancelCheck cancel,void*ctx){
 uint8_t buf[BUFFER_SIZE];Cmp5Sha256 sha;cmp5_sha256_init(&sha);
 for(uint64_t done=0;done<bytes;){
  if(canceled(cancel,ctx))return 0;
  size_t take=(size_t)(bytes-done>sizeof(buf)?sizeof(buf):bytes-done);
  if(!full_read(source,buf,take)||!full_write(dest,buf,take))return 0;
  cmp5_sha256_update(&sha,buf,take);done+=take;
 }
 cmp5_sha256_final(&sha,digest);
 return !canceled(cancel,ctx);
}
static int start_stage(int dest,const char*id,char temp[56]){
 if(!valid_id(id))return -1;
 temp[0]='.';
 size_t n=strlen(id);memcpy(temp+1,id,n);
 memcpy(temp+n+1,".part",6);
 return openat(dest,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
}
static int publish(int dir,const char*temp,const char*id){
 if(linkat(dir,temp,dir,id,0)!=0)return 0; /* no-clobber, atomic */
 /* A residual .part hardlink means this namespace is not transactionally
  * clean; do not present success after failed staging cleanup. */
 if(unlinkat(dir,temp,0)!=0)return 0; /* app-owned staging only */
 /* Successful fsync of the file alone does not ensure its directory entry
  * survives a power loss. Refuse to issue a durable receipt if this fails. */
 return fsync(dir)==0;
}
static int read_header(int fd,uint64_t*len,uint8_t digest[32]){
 uint8_t header[HEADER_SIZE];
 if(!full_read(fd,header,sizeof(header))||memcmp(header,magic,8)!=0)return 0;
 for(unsigned i=48;i<64;i++)if(header[i])return 0;
 *len=get64(header+8);
 if(*len==0||*len>CMP5_BACKUP_LIMIT)return 0;
 memcpy(digest,header+16,32);
 return 1;
}
int cmp5_backup_media(const char*root,const Cmp5MediaCandidate*c,
 const char*workspace,const char*id,Cmp5BackupReceipt*r,
 Cmp5CancelCheck cancel,void*ctx){
 if(r)memset(r,0,sizeof(*r));
 if(!r||!valid_id(id)||canceled(cancel,ctx))return 0;
 struct stat before,after;
 int source=source_file(root,c,&before);
 if(source<0)return 0;
 int dir=private_dir(workspace);
 if(dir<0){close(source);return 0;}
 struct stat destination_root={0};
 if(fstat(dir,&destination_root)!=0||
    !cmp5_workspace_has_capacity(dir,c->bytes,1u)){
  close(source);close(dir);return 0;
 }
 char temp[56];int stage=start_stage(dir,id,temp);
 if(stage<0){close(source);close(dir);return 0;}
 uint8_t header[HEADER_SIZE]={0};memcpy(header,magic,8);put64(header+8,c->bytes);
 int ok=full_write(stage,header,sizeof(header));
 uint8_t digest[32]={0};
 if(ok)ok=copy_body(source,stage,c->bytes,digest,cancel,ctx);
 if(ok)ok=fstat(source,&after)==0&&same(&before,&after)&&
    snapshot_matches(&after,c);
 /* A stable file descriptor is insufficient if the selected filename has
  * been renamed or replaced while bytes were copied. Reopen the anchored
  * gallery path and demand the SAME inode before publishing a capsule.
  * This only reads the source; it never touches PS5 gallery contents. */
 if(ok){
  struct stat current;
  const int named=source_file(root,c,&current);
  ok=named>=0&&same(&before,&current);
  if(named>=0)close(named);
 }
 close(source);
 if(ok){memcpy(header+16,digest,32);ok=lseek(stage,0,SEEK_SET)==0&&
     full_write(stage,header,sizeof(header))&&fsync(stage)==0;}
 close(stage);
 if(ok&&canceled(cancel,ctx))ok=0;
 if(ok)ok=private_directory_identity_current(dir,workspace,&destination_root);
 if(ok)ok=publish(dir,temp,id);
 /* Do not issue a success receipt for a detached workspace or a
  * replacement capsule under the just-published name. Re-open and hash
  * the published artifact independently before returning success. */
 if(ok){
  Cmp5BackupReceipt checked={0};
  ok=cmp5_verify_backup(workspace,id,&checked,cancel,ctx)&&
    checked.published&&checked.bytes==c->bytes&&
    memcmp(checked.sha256,digest,32)==0&&
    private_directory_identity_current(dir,workspace,&destination_root);
 }
 if(ok){r->published=1;r->bytes=c->bytes;memcpy(r->sha256,digest,32);}
 if(!ok)(void)unlinkat(dir,temp,0);
 close(dir);
 return ok;
}
static int verify_open(int fd,Cmp5BackupReceipt*r,Cmp5CancelCheck cancel,void*ctx){
 struct stat before,after;
 if(fstat(fd,&before)!=0||!S_ISREG(before.st_mode)||before.st_nlink!=1||
    before.st_size<(off_t)HEADER_SIZE)return 0;
 uint64_t count=0;uint8_t expected[32];
 if(!read_header(fd,&count,expected)||
    (uint64_t)before.st_size!=count+HEADER_SIZE)return 0;
 uint8_t buf[BUFFER_SIZE],digest[32];Cmp5Sha256 sha;cmp5_sha256_init(&sha);
 for(uint64_t done=0;done<count;){
  if(canceled(cancel,ctx))return 0;
  const size_t chunk=(size_t)(count-done>sizeof(buf)?sizeof(buf):count-done);
  if(!full_read(fd,buf,chunk))return 0;
  cmp5_sha256_update(&sha,buf,chunk);done+=chunk;
 }
 cmp5_sha256_final(&sha,digest);
 if(memcmp(digest,expected,32)!=0||fstat(fd,&after)!=0||
    !same(&before,&after)||canceled(cancel,ctx))return 0;
 if(r){memset(r,0,sizeof(*r));r->published=1;r->bytes=count;
  memcpy(r->sha256,digest,32);}
 return 1;
}
int cmp5_verify_backup(const char*workspace,const char*id,
 Cmp5BackupReceipt*r,Cmp5CancelCheck cancel,void*ctx){
 if(r)memset(r,0,sizeof(*r));
 if(!valid_id(id))return 0;
 int dir=private_dir(workspace);if(dir<0)return 0;
 struct stat dir_before={0};
 if(fstat(dir,&dir_before)!=0){close(dir);return 0;}
 int fd=openat(dir,id,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 if(fd<0){close(dir);return 0;}
 struct stat before,after,named;
 Cmp5BackupReceipt verified={0};
 int ok=fstat(fd,&before)==0&&
  verify_open(fd,&verified,cancel,ctx)&&
  fstat(fd,&after)==0&&same(&before,&after)&&
  fstatat(dir,id,&named,AT_SYMLINK_NOFOLLOW)==0&&
  same(&before,&named)&&named.st_nlink==1&&
  private_directory_current(dir,workspace,&dir_before);
 close(fd);close(dir);
 if(ok&&r)*r=verified;
 return ok;
}
/* A published restore must be re-read by its destination name. Verifying
 * only the bytes written to the staging descriptor is not sufficient:
 * a rename or replacement after close could publish another object. */
static int verify_restored_file(int dir,const char*id,
 const Cmp5BackupReceipt*expected,Cmp5CancelCheck cancel,void*ctx){
 int fd=openat(dir,id,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 if(fd<0)return 0;
 struct stat before={0},after={0},named={0};
 int ok=fstat(fd,&before)==0&&S_ISREG(before.st_mode)&&
   before.st_nlink==1&&before.st_size>0&&
   (uint64_t)before.st_size==expected->bytes;
 uint8_t data[BUFFER_SIZE],digest[32]={0};
 Cmp5Sha256 sha;cmp5_sha256_init(&sha);
 for(uint64_t done=0;ok&&done<expected->bytes;){
  if(canceled(cancel,ctx)){ok=0;break;}
  const size_t n=(size_t)(expected->bytes-done>sizeof(data)?
                         sizeof(data):expected->bytes-done);
  if(!full_read(fd,data,n)){ok=0;break;}
  cmp5_sha256_update(&sha,data,n);
  done+=n;
 }
 if(ok)cmp5_sha256_final(&sha,digest);
 if(ok)ok=memcmp(digest,expected->sha256,32)==0&&
    fstat(fd,&after)==0&&same(&before,&after)&&after.st_nlink==1&&
    fstatat(dir,id,&named,AT_SYMLINK_NOFOLLOW)==0&&
    same(&before,&named)&&named.st_nlink==1&&!canceled(cancel,ctx);
 close(fd);
 return ok;
}
int cmp5_restore_backup(const char*backup,const char*id,
 const char*destination,const char*newname,Cmp5BackupReceipt*r,
 Cmp5CancelCheck cancel,void*ctx){
 if(r)memset(r,0,sizeof(*r));
 if(!valid_id(id)||!valid_id(newname)||canceled(cancel,ctx))return 0;
 int source_dir=private_dir(backup);if(source_dir<0)return 0;
 int source=openat(source_dir,id,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 if(source<0){close(source_dir);return 0;}
 struct stat before,after,named,source_root,target_root;
 Cmp5BackupReceipt verified={0};
 if(fstat(source,&before)!=0||
    !verify_open(source,&verified,cancel,ctx)||
    fstat(source,&after)!=0||!same(&before,&after)){
  close(source);close(source_dir);return 0;
 }
 int target=private_dir(destination);
 if(target<0){close(source);close(source_dir);return 0;}
 /* An alias such as /workspace/. must never qualify as independent restore
  * storage, regardless of the spelling of source and destination paths. */
 if(fstat(source_dir,&source_root)!=0||fstat(target,&target_root)!=0||
    (source_root.st_dev==target_root.st_dev&&
     source_root.st_ino==target_root.st_ino)||
    !cmp5_workspace_has_capacity(target,verified.bytes,1u)){
  close(source);close(source_dir);close(target);return 0;
 }
 if(lseek(source,HEADER_SIZE,SEEK_SET)!=(off_t)HEADER_SIZE){
  close(source);close(source_dir);close(target);return 0;
 }
 char temp[56];int stage=start_stage(target,newname,temp);
 if(stage<0){close(source);close(source_dir);close(target);return 0;}
 uint8_t digest[32];
 int ok=copy_body(source,stage,verified.bytes,digest,cancel,ctx)&&
   memcmp(digest,verified.sha256,32)==0;
 /* Hash equality does not prove that a capsule still exists under its
  * original path. Reject source removal, link or inode replacement. */
 if(ok)ok=fstat(source,&after)==0&&same(&before,&after)&&
   fstatat(source_dir,id,&named,AT_SYMLINK_NOFOLLOW)==0&&
   same(&before,&named)&&named.st_nlink==1;
 /* A descriptor can remain valid after its parent directory is replaced.
  * Refuse to publish into an orphaned destination or from an orphaned
  * backup workspace; neither pathname may silently change identity. */
 if(ok)ok=private_directory_identity_current(source_dir,backup,&source_root)&&
    private_directory_identity_current(target,destination,&target_root);
 if(ok)ok=fsync(stage)==0;
 close(stage);
 if(ok&&canceled(cancel,ctx))ok=0;
 if(ok)ok=private_directory_identity_current(source_dir,backup,&source_root)&&
    private_directory_identity_current(target,destination,&target_root);
 if(ok)ok=publish(target,temp,newname);
 if(ok)ok=verify_restored_file(target,newname,&verified,cancel,ctx)&&
    fstat(source,&after)==0&&same(&before,&after)&&
    fstatat(source_dir,id,&named,AT_SYMLINK_NOFOLLOW)==0&&
    same(&before,&named)&&named.st_nlink==1&&
    private_directory_identity_current(source_dir,backup,&source_root)&&
    private_directory_identity_current(target,destination,&target_root);
 if(ok&&r)*r=verified;
 if(!ok)(void)unlinkat(target,temp,0);
 close(source);close(source_dir);
 close(target);
 return ok;
}
