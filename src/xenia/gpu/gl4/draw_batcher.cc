/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/draw_batcher.h"

#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/gl4/gl4_gpu_flags.h"
#include "xenia/gpu/gpu_flags.h"

namespace xe {
namespace gpu {
namespace gl4 {

using namespace xe::gpu::xenos;

const size_t kCommandBufferCapacity = 16 * (1024 * 1024);
const size_t kCommandBufferAlignment = 4;
const size_t kStateBufferCapacity = 64 * (1024 * 1024);
const size_t kStateBufferAlignment = 256;

DrawBatcher::DrawBatcher(RegisterFile* register_file)
    : register_file_(register_file),
      command_buffer_(kCommandBufferCapacity, kCommandBufferAlignment),
      state_buffer_(kStateBufferCapacity, kStateBufferAlignment),
      array_data_buffer_(nullptr),
      draw_open_(false) {
  std::memset(&batch_state_, 0, sizeof(batch_state_));
  batch_state_.needs_reconfigure = true;
  batch_state_.command_range_start = batch_state_.state_range_start =
      UINTPTR_MAX;
  std::memset(&active_draw_, 0, sizeof(active_draw_));
}

bool DrawBatcher::Initialize(CircularBuffer* array_data_buffer) {
  array_data_buffer_ = array_data_buffer;
  if (!command_buffer_.Initialize()) {
    return false;
  }
  if (!state_buffer_.Initialize()) {
    return false;
  }
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, command_buffer_.handle());
  return true;
}

void DrawBatcher::Shutdown() {
  command_buffer_.Shutdown();
  state_buffer_.Shutdown();
}

bool DrawBatcher::ReconfigurePipeline(GL4Shader* vertex_shader,
                                      GL4Shader* pixel_shader,
                                      GLuint pipeline) {
  if (batch_state_.pipeline == pipeline) {
    // No-op.
    return true;
  }
  if (!Flush(FlushMode::kReconfigure)) {
    return false;
  }

  batch_state_.vertex_shader = vertex_shader;
  batch_state_.pixel_shader = pixel_shader;
  batch_state_.pipeline = pipeline;

  return true;
}

bool DrawBatcher::BeginDrawArrays(PrimitiveType prim_type,
                                  uint32_t index_count) {
  assert_false(draw_open_);
  if (batch_state_.prim_type != prim_type || batch_state_.indexed) {
    if (!Flush(FlushMode::kReconfigure)) {
      return false;
    }
  }
  batch_state_.prim_type = prim_type;
  batch_state_.indexed = false;

  if (!BeginDraw()) {
    return false;
  }

  auto cmd = active_draw_.draw_arrays_cmd;
  cmd->base_instance = 0;
  cmd->instance_count = 1;
  cmd->count = index_count;
  cmd->first_index = 0;

  return true;
}

bool DrawBatcher::BeginDrawElements(PrimitiveType prim_type,
                                    uint32_t index_count,
                                    IndexFormat index_format) {
  assert_false(draw_open_);
  GLenum index_type =
      index_format == IndexFormat::kInt32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
  if (batch_state_.prim_type != prim_type || !batch_state_.indexed ||
      batch_state_.index_type != index_type) {
    if (!Flush(FlushMode::kReconfigure)) {
      return false;
    }
  }
  batch_state_.prim_type = prim_type;
  batch_state_.indexed = true;
  batch_state_.index_type = index_type;

  if (!BeginDraw()) {
    return false;
  }

  uint32_t start_index = register_file_->values[XE_GPU_REG_VGT_INDX_OFFSET].u32;
  assert_zero(start_index);

  auto cmd = active_draw_.draw_elements_cmd;
  cmd->base_instance = 0;
  cmd->instance_count = 1;
  cmd->count = index_count;
  cmd->first_index = start_index;
  cmd->base_vertex = 0;

  return true;
}

bool DrawBatcher::BeginDraw() {
  draw_open_ = true;

  if (batch_state_.needs_reconfigure) {
    batch_state_.needs_reconfigure = false;
    // Have been reconfigured since last draw - need to compute state size.
    // Layout:
    //   [draw command]
    //   [common header]
    //   [consts]

    // Padded to max.
    GLsizei command_size = 0;
    if (batch_state_.indexed) {
      command_size = sizeof(DrawElementsIndirectCommand);
    } else {
      command_size = sizeof(DrawArraysIndirectCommand);
    }
    batch_state_.command_stride =
        xe::round_up(command_size, GLsizei(kCommandBufferAlignment));

    GLsizei header_size = sizeof(CommonHeader);

    // TODO(benvanik): consts sizing.
    // GLsizei float_consts_size = sizeof(float4) * 512;
    // GLsizei bool_consts_size = sizeof(uint32_t) * 8;
    // GLsizei loop_consts_size = sizeof(uint32_t) * 32;
    // GLsizei consts_size =
    //    float_consts_size + bool_consts_size + loop_consts_size;
    // batch_state_.float_consts_offset = batch_state_.header_offset +
    // header_size;
    // batch_state_.bool_consts_offset =
    //    batch_state_.float_consts_offset + float_consts_size;
    // batch_state_.loop_consts_offset =
    //    batch_state_.bool_consts_offset + bool_consts_size;
    GLsizei consts_size = 0;

    batch_state_.state_stride = header_size + consts_size;
  }

  // Allocate a command data block.
  // We should treat it as write-only.
  if (!command_buffer_.CanAcquire(batch_state_.command_stride)) {
    Flush(FlushMode::kMakeCoherent);
  }
  active_draw_.command_allocation =
      command_buffer_.Acquire(batch_state_.command_stride);
  assert_not_null(active_draw_.command_allocation.host_ptr);

  // Allocate a state data block.
  // We should treat it as write-only.
  if (!state_buffer_.CanAcquire(batch_state_.state_stride)) {
    Flush(FlushMode::kMakeCoherent);
  }
  active_draw_.state_allocation =
      state_buffer_.Acquire(batch_state_.state_stride);
  assert_not_null(active_draw_.state_allocation.host_ptr);

  active_draw_.command_address =
      reinterpret_cast<uintptr_t>(active_draw_.command_allocation.host_ptr);
  auto state_host_ptr =
      reinterpret_cast<uintptr_t>(active_draw_.state_allocation.host_ptr);
  active_draw_.header = reinterpret_cast<CommonHeader*>(state_host_ptr);
  active_draw_.header->ps_param_gen = -1;
  // active_draw_.float_consts =
  //    reinterpret_cast<float4*>(state_host_ptr +
  //    batch_state_.float_consts_offset);
  // active_draw_.bool_consts =
  //    reinterpret_cast<uint32_t*>(state_host_ptr +
  //    batch_state_.bool_consts_offset);
  // active_draw_.loop_consts =
  //    reinterpret_cast<uint32_t*>(state_host_ptr +
  //    batch_state_.loop_consts_offset);
  return true;
}

void DrawBatcher::DiscardDraw() {
  if (!draw_open_) {
    // No-op.
    return;
  }
  draw_open_ = false;

  command_buffer_.Discard(std::move(active_draw_.command_allocation));
  state_buffer_.Discard(std::move(active_draw_.state_allocation));
}

bool DrawBatcher::CommitDraw() {
  assert_true(draw_open_);
  draw_open_ = false;

  // Copy over required constants.
  CopyConstants();

  if (batch_state_.state_range_start == UINTPTR_MAX) {
    batch_state_.command_range_start = active_draw_.command_allocation.offset;
    batch_state_.state_range_start = active_draw_.state_allocation.offset;
  }
  batch_state_.command_range_length +=
      active_draw_.command_allocation.aligned_length;
  batch_state_.state_range_length +=
      active_draw_.state_allocation.aligned_length;

  command_buffer_.Commit(std::move(active_draw_.command_allocation));
  state_buffer_.Commit(std::move(active_draw_.state_allocation));

  ++batch_state_.draw_count;
  return true;
}

bool DrawBatcher::Flush(FlushMode mode) {
  GLboolean cull_enabled = 0;
  if (batch_state_.draw_count) {
#if FINE_GRAINED_DRAW_SCOPES
    SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

    assert_not_zero(batch_state_.command_stride);
    assert_not_zero(batch_state_.state_stride);

    // Flush pending buffer changes.
    command_buffer_.Flush();
    state_buffer_.Flush();
    array_data_buffer_->Flush();

    // State data is indexed by draw ID.
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, state_buffer_.handle(),
                      batch_state_.state_range_start,
                      batch_state_.state_range_length);

    GLenum prim_type = 0;
    switch (batch_state_.prim_type) {
      case PrimitiveType::kPointList:
        prim_type = GL_POINTS;
        break;
      case PrimitiveType::kLineList:
        prim_type = GL_LINES;
        break;
      case PrimitiveType::kLineStrip:
        prim_type = GL_LINE_STRIP;
        break;
      case PrimitiveType::kLineLoop:
        prim_type = GL_LINE_LOOP;
        break;
      case PrimitiveType::kTriangleList:
        prim_type = GL_TRIANGLES;
        break;
      case PrimitiveType::kTriangleStrip:
        prim_type = GL_TRIANGLE_STRIP;
        break;
      case PrimitiveType::kTriangleFan:
        prim_type = GL_TRIANGLE_FAN;
        break;
      case PrimitiveType::kRectangleList:
        prim_type = GL_TRIANGLES;
        // Rect lists aren't culled. There may be other things they skip too.
        // assert_true(
        // (register_file_->values[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32
        // & 0x3) == 0);
        cull_enabled = glIsEnabled(GL_CULL_FACE);
        glDisable(GL_CULL_FACE);
        break;
      case PrimitiveType::kQuadList:
        prim_type = GL_LINES_ADJACENCY;
        break;
      default:
      case PrimitiveType::kUnknown0x07:
        prim_type = GL_POINTS;
        XELOGE("unsupported primitive type %d", batch_state_.prim_type);
        assert_unhandled_case(batch_state_.prim_type);
        DiscardDraw();
        return false;
    }

    // Fast path for single draws.
    void* indirect_offset =
        reinterpret_cast<void*>(batch_state_.command_range_start);

    if (batch_state_.draw_count == 1) {
      // Fast path for one draw. Removes MDI overhead when not required.
      if (batch_state_.indexed) {
        auto& cmd = active_draw_.draw_elements_cmd;
        glDrawElementsInstancedBaseVertexBaseInstance(
            prim_type, cmd->count, batch_state_.index_type,
            reinterpret_cast<void*>(
                uintptr_t(cmd->first_index) *
                (batch_state_.index_type == GL_UNSIGNED_SHORT ? 2 : 4)),
            cmd->instance_count, cmd->base_vertex, cmd->base_instance);
      } else {
        auto& cmd = active_draw_.draw_arrays_cmd;
        glDrawArraysInstancedBaseInstance(prim_type, cmd->first_index,
                                          cmd->count, cmd->instance_count,
                                          cmd->base_instance);
      }
    } else {
      // Full multi-draw.
      if (batch_state_.indexed) {
        glMultiDrawElementsIndirect(prim_type, batch_state_.index_type,
                                    indirect_offset, batch_state_.draw_count,
                                    batch_state_.command_stride);
      } else {
        glMultiDrawArraysIndirect(prim_type, indirect_offset,
                                  batch_state_.draw_count,
                                  batch_state_.command_stride);
      }
    }

    batch_state_.command_range_start = UINTPTR_MAX;
    batch_state_.command_range_length = 0;
    batch_state_.state_range_start = UINTPTR_MAX;
    batch_state_.state_range_length = 0;
    batch_state_.draw_count = 0;
  }

  if (batch_state_.prim_type == PrimitiveType::kRectangleList && cull_enabled) {
    glEnable(GL_CULL_FACE);
  }

  if (mode == FlushMode::kReconfigure) {
    // Reset - we'll update it as soon as we have all the information.
    batch_state_.needs_reconfigure = true;
  }

  return true;
}

void DrawBatcher::CopyConstants() {
  // TODO(benvanik): partial updates, etc. We could use shader constant access
  // knowledge that we get at compile time to only upload those constants
  // required. If we did this as a variable length then we could really cut
  // down on state block sizes.

  std::memcpy(active_draw_.header->float_consts,
              &register_file_->values[XE_GPU_REG_SHADER_CONSTANT_000_X].f32,
              sizeof(active_draw_.header->float_consts));
  std::memcpy(
      active_draw_.header->bool_consts,
      &register_file_->values[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031].f32,
      sizeof(active_draw_.header->bool_consts));
  std::memcpy(active_draw_.header->loop_consts,
              &register_file_->values[XE_GPU_REG_SHADER_CONSTANT_LOOP_00].f32,
              sizeof(active_draw_.header->loop_consts));
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
