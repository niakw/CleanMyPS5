#ifndef CMP5_WEBM_METADATA_H
#define CMP5_WEBM_METADATA_H
#include <stdint.h>
#include "video_metadata.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Parses only EBML/WebM Info/Tracks, skipping Cluster media bytes. */
int cmp5_parse_webm_metadata_fd(int fd,uint64_t length,
 Cmp5VideoMetadata*out,Cmp5CancelCheck cancel,void*context);
#ifdef __cplusplus
}
#endif
#endif
