/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define XE_TEXTURE_LOAD_RESOLUTION_SCALED
#include "pixel_formats.xesli"
#define XE_TEXTURE_LOAD_32BPB_TRANSFORM(blocks) \
  (XeFloat20e4To32((blocks) >> 8u))
#include "texture_load_32bpb.xesli"
