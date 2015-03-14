/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GL4_GL4_STATE_DATA_BUILDER_H_
#define XENIA_GPU_GL4_GL4_STATE_DATA_BUILDER_H_

#include "xenia/common.h"
#include "xenia/gpu/gl4/circular_buffer.h"
#include "xenia/gpu/gl4/gl_context.h"
#include "xenia/gpu/gl4/gl4_shader.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
namespace gl4 {

union float4 {
  float v[4];
  struct {
    float x, y, z, w;
  };
};

#pragma pack(push, 4)
struct DrawArraysIndirectCommand {
  GLuint count;
  GLuint instance_count;
  GLuint first_index;
  GLuint base_instance;
};
struct DrawElementsIndirectCommand {
  GLuint count;
  GLuint instance_count;
  GLuint first_index;
  GLint base_vertex;
  GLuint base_instance;
};
struct BindlessPtrNV {
  GLuint index;
  GLuint reserved_zero;
  GLuint64 address;
  GLuint64 length;
};
struct DrawArraysIndirectBindlessCommandNV {
  DrawArraysIndirectCommand cmd;
  // NOTE: the spec is wrong here. For fucks sake.
  // GLuint reserved_zero;
  BindlessPtrNV vertex_buffers[8];
};
struct DrawElementsIndirectBindlessCommandNV {
  DrawElementsIndirectCommand cmd;
  GLuint reserved_zero;
  BindlessPtrNV index_buffer;
  BindlessPtrNV vertex_buffers[8];
};
#pragma pack(pop)

class DrawBatcher {
 public:
  enum class FlushMode {
    kMakeCoherent,
    kStateChange,
    kReconfigure,
  };

  DrawBatcher(RegisterFile* register_file);

  bool Initialize(CircularBuffer* array_data_buffer);
  void Shutdown();

  PrimitiveType prim_type() const { return batch_state_.prim_type; }

  void set_window_offset(uint32_t x, uint32_t y) {
    active_draw_.header->window_offset.x = float(x);
    active_draw_.header->window_offset.y = float(y);
  }
  void set_window_scalar(float width_scalar, float height_scalar) {
    active_draw_.header->window_offset.z = width_scalar;
    active_draw_.header->window_offset.w = height_scalar;
  }
  void set_vtx_fmt(float xy, float z, float w) {
    active_draw_.header->vtx_fmt.x = xy;
    active_draw_.header->vtx_fmt.y = xy;
    active_draw_.header->vtx_fmt.z = z;
    active_draw_.header->vtx_fmt.w = w;
  }
  void set_alpha_test(bool enabled, uint32_t func, float ref) {
    active_draw_.header->alpha_test.x = enabled ? 1.0f : 0.0f;
    active_draw_.header->alpha_test.y = float(func);
    active_draw_.header->alpha_test.z = ref;
  }
  void set_texture_sampler(int index, GLuint64 handle) {
    active_draw_.header->texture_samplers[index] = handle;
  }
  void set_index_buffer(const CircularBuffer::Allocation& allocation) {
    if (has_bindless_mdi_) {
      auto& ptr = active_draw_.draw_elements_bindless_cmd->index_buffer;
      ptr.reserved_zero = 0;
      ptr.index = 0;
      ptr.address = allocation.gpu_ptr;
      ptr.length = allocation.length;
    } else {
      // Offset is used in glDrawElements.
      auto& cmd = active_draw_.draw_elements_cmd;
      size_t index_size = batch_state_.index_type == GL_UNSIGNED_SHORT ? 2 : 4;
      cmd->first_index = GLuint(allocation.offset / index_size);
    }
  }
  void set_vertex_buffer(int index, GLsizei offset, GLsizei stride,
                         const CircularBuffer::Allocation& allocation) {
    if (has_bindless_mdi_) {
      BindlessPtrNV* ptr;
      if (batch_state_.indexed) {
        ptr = &active_draw_.draw_elements_bindless_cmd->vertex_buffers[index];
      } else {
        ptr = &active_draw_.draw_arrays_bindless_cmd->vertex_buffers[index];
      }
      ptr->reserved_zero = 0;
      ptr->index = index;
      ptr->address = allocation.gpu_ptr + offset;
      ptr->length = allocation.length - offset;
    }
  }

  bool ReconfigurePipeline(GL4Shader* vertex_shader, GL4Shader* pixel_shader,
                           GLuint pipeline);

  bool BeginDrawArrays(PrimitiveType prim_type, uint32_t index_count);
  bool BeginDrawElements(PrimitiveType prim_type, uint32_t index_count,
                         xenos::IndexFormat index_format);
  void DiscardDraw();
  bool CommitDraw();
  bool Flush(FlushMode mode);

 private:
  bool BeginDraw();
  void CopyConstants();

  RegisterFile* register_file_;
  CircularBuffer command_buffer_;
  CircularBuffer state_buffer_;
  CircularBuffer* array_data_buffer_;

  bool has_bindless_mdi_;

  struct BatchState {
    bool needs_reconfigure;
    PrimitiveType prim_type;
    bool indexed;
    GLenum index_type;

    GL4Shader* vertex_shader;
    GL4Shader* pixel_shader;
    GLuint pipeline;

    GLsizei command_stride;
    GLsizei state_stride;
    GLsizei float_consts_offset;
    GLsizei bool_consts_offset;
    GLsizei loop_consts_offset;

    uintptr_t command_range_start;
    uintptr_t command_range_length;
    uintptr_t state_range_start;
    uintptr_t state_range_length;
    GLsizei draw_count;
  } batch_state_;

  // This must match GL4Shader's header.
  struct CommonHeader {
    float4 window_offset;    // tx,ty,sx,sy
    float4 vtx_fmt;          //
    float4 alpha_test;       // alpha test enable, func, ref, ?

    // TODO(benvanik): pack tightly
    GLuint64 texture_samplers[32];

    float4 float_consts[512];
    uint32_t bool_consts[8];
    uint32_t loop_consts[32];
  };
  struct {
    CircularBuffer::Allocation command_allocation;
    CircularBuffer::Allocation state_allocation;

    union {
      DrawArraysIndirectCommand* draw_arrays_cmd;
      DrawElementsIndirectCommand* draw_elements_cmd;
      DrawArraysIndirectBindlessCommandNV* draw_arrays_bindless_cmd;
      DrawElementsIndirectBindlessCommandNV* draw_elements_bindless_cmd;
      uintptr_t command_address;
    };

    CommonHeader* header;
  } active_draw_;
  bool draw_open_;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_GL4_STATE_DATA_BUILDER_H_
