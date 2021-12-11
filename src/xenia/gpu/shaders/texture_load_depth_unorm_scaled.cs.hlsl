#define XE_TEXTURE_LOAD_RESOLUTION_SCALED
#include "pixel_formats.hlsli"
#define XE_TEXTURE_LOAD_32BPB_TRANSFORM(blocks) \
  (asuint(XeUNorm24To32((blocks) >> 8u)))
#include "texture_load_32bpb.hlsli"
