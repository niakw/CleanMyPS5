#ifndef CMP5_VIDEO_H
#define CMP5_VIDEO_H
#include <stddef.h>
#include <stdint.h>
/* Open only an original MP4/WebM regular file in the capture-video tree.
 * fd ownership transfers to the caller on 200; never opens for writing.
 * Caller MUST close(fd), including on a failed subsequent HTTP transfer. */
int cmp5_open_video(const char *root, const char *absolute_path,
                    int *fd, uint64_t *bytes, const char **mime);
#endif
