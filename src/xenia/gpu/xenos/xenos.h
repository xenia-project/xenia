/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_XENOS_XENOS_H_
#define XENIA_GPU_XENOS_XENOS_H_

#include <xenia/core.h>


namespace xe {
namespace gpu {
namespace xenos {


typedef enum {
  XE_GPU_SHADER_TYPE_VERTEX               = 0x00,
  XE_GPU_SHADER_TYPE_PIXEL                = 0x01,
} XE_GPU_SHADER_TYPE;

typedef enum {
  XE_GPU_INVALIDATE_MASK_VERTEX_SHADER    = 1 << 8,
  XE_GPU_INVALIDATE_MASK_PIXEL_SHADER     = 1 << 9,

  XE_GPU_INVALIDATE_MASK_ALL              = 0x7FFF,
} XE_GPU_INVALIDATE_MASK;

typedef enum {
  XE_GPU_PRIMITIVE_TYPE_POINT_LIST        = 0x01,
  XE_GPU_PRIMITIVE_TYPE_LINE_LIST         = 0x02,
  XE_GPU_PRIMITIVE_TYPE_LINE_STRIP        = 0x03,
  XE_GPU_PRIMITIVE_TYPE_TRIANGLE_LIST     = 0x04,
  XE_GPU_PRIMITIVE_TYPE_TRIANGLE_FAN      = 0x05,
  XE_GPU_PRIMITIVE_TYPE_TRIANGLE_STRIP    = 0x06,
  XE_GPU_PRIMITIVE_TYPE_UNKNOWN_07        = 0x07,
  XE_GPU_PRIMITIVE_TYPE_RECTANGLE_LIST    = 0x08,
  XE_GPU_PRIMITIVE_TYPE_LINE_LOOP         = 0x0C,
} XE_GPU_PRIMITIVE_TYPE;


}  // namespace xenos
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_XENOS_XENOS_H_
