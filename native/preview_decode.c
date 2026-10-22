#include "preview_decode.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_SIMD
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define MAX_INPUT (16u*1024u*1024u)
#define MAX_PIXELS (12000000u)
#define PREVIEW_MAX_WIDTH 1100u
#define PREVIEW_MAX_HEIGHT 620u

void cmp5_release_preview(Cmp5PreviewImage *image){
 if(!image)return;
 free(image->pixels);
 memset(image,0,sizeof(*image));
}
int cmp5_decode_preview(const uint8_t *data,size_t length,Cmp5PreviewImage *out){
 if(!out)return 0;
 memset(out,0,sizeof(*out));
 if(!data||length<16u||length>MAX_INPUT)return 0;
 int width=0,height=0,channels=0;
 if(!stbi_info_from_memory(data,(int)length,&width,&height,&channels)||
    width<=0||height<=0||(uint64_t)width*(uint64_t)height>MAX_PIXELS)return 0;
 int decoded_w=0,decoded_h=0,decoded_channels=0;
 unsigned char *rgba=stbi_load_from_memory(data,(int)length,
   &decoded_w,&decoded_h,&decoded_channels,4);
 if(!rgba)return 0;
 if(decoded_w!=width||decoded_h!=height){stbi_image_free(rgba);return 0;}
 uint32_t w=(uint32_t)width,h=(uint32_t)height;
 uint32_t target_w=w,target_h=h;
 if(w>PREVIEW_MAX_WIDTH){
  target_h=(uint32_t)(((uint64_t)h*PREVIEW_MAX_WIDTH)/w);
  target_w=PREVIEW_MAX_WIDTH;
 }
 if(target_h>PREVIEW_MAX_HEIGHT){
  target_w=(uint32_t)(((uint64_t)target_w*PREVIEW_MAX_HEIGHT)/target_h);
  target_h=PREVIEW_MAX_HEIGHT;
 }
 if(target_w==0)target_w=1;
 if(target_h==0)target_h=1;
 if((uint64_t)target_w*target_h>SIZE_MAX/sizeof(uint32_t)){
  stbi_image_free(rgba);return 0;
 }
 uint32_t*pixels=(uint32_t*)malloc((size_t)target_w*target_h*sizeof(uint32_t));
 if(!pixels){stbi_image_free(rgba);return 0;}
 // Bilinear downsampling is not required to show content; nearest neighbor
 // is deterministic and keeps latency low for the console's CPU renderer.
 for(uint32_t y=0;y<target_h;y++){
  const uint32_t source_y=(uint32_t)(((uint64_t)y*h)/target_h);
  for(uint32_t x=0;x<target_w;x++){
   const uint32_t source_x=(uint32_t)(((uint64_t)x*w)/target_w);
   const unsigned char*src=rgba+((size_t)source_y*w+source_x)*4u;
   pixels[(size_t)y*target_w+x]=(uint32_t)src[0]|
     ((uint32_t)src[1]<<8)|((uint32_t)src[2]<<16)|
     ((uint32_t)src[3]<<24);
  }
 }
 stbi_image_free(rgba);
 out->width=target_w;out->height=target_h;out->pixels=pixels;
 return 1;
}
