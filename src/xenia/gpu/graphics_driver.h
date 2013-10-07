/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GRAPHICS_DRIVER_H_
#define XENIA_GPU_GRAPHICS_DRIVER_H_

#include <xenia/core.h>
#include <xenia/gpu/xenos/registers.h>


namespace xe {
namespace gpu {


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


class GraphicsDriver {
public:
  virtual ~GraphicsDriver();

  xe_memory_ref memory();
  xenos::RegisterFile* register_file() { return &register_file_; };

  virtual void Initialize() = 0;

  virtual void InvalidateState(
      uint32_t mask) = 0;
  virtual void SetShader(
      XE_GPU_SHADER_TYPE type,
      uint32_t address,
      uint32_t start,
      uint32_t size_dwords) = 0;
  virtual void DrawIndexed(
      XE_GPU_PRIMITIVE_TYPE prim_type,
      uint32_t index_count) = 0;

protected:
  GraphicsDriver(xe_memory_ref memory);

  xe_memory_ref memory_;

  xenos::RegisterFile register_file_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_GRAPHICS_DRIVER_H_
