/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_DRAW_COMMAND_H_
#define XENIA_GPU_DRAW_COMMAND_H_

#include <xenia/core.h>
#include <xenia/gpu/buffer_resource.h>
#include <xenia/gpu/sampler_state_resource.h>
#include <xenia/gpu/shader_resource.h>
#include <xenia/gpu/texture_resource.h>
#include <xenia/gpu/xenos/xenos.h>


namespace xe {
namespace gpu {


// TODO(benvanik): move more of the enums in here?
struct DrawCommand {
  xenos::XE_GPU_PRIMITIVE_TYPE prim_type;
  uint32_t start_index;
  uint32_t index_count;
  uint32_t base_vertex;

  VertexShaderResource* vertex_shader;
  PixelShaderResource* pixel_shader;

  // TODO(benvanik): dirty tracking/max ranges/etc.
  struct {
    float* values;
    size_t count;
  } float4_constants;
  struct {
    uint32_t* values;
    size_t count;
  } loop_constants;
  struct {
    uint32_t* values;
    size_t count;
  } bool_constants;

  // Index buffer, if present. If index_count > 0 then auto draw.
  IndexBufferResource* index_buffer;

  // Vertex buffers.
  struct {
    uint32_t input_index;
    VertexBufferResource* buffer;
    uint32_t stride;
    uint32_t offset;
  } vertex_buffers[96];
  size_t vertex_buffer_count;

  // Texture samplers.
  struct SamplerInput {
    uint32_t input_index;
    TextureResource* texture;
    SamplerStateResource* sampler_state;
  };
  SamplerInput vertex_shader_samplers[32];
  size_t vertex_shader_sampler_count;
  SamplerInput pixel_shader_samplers[32];
  size_t pixel_shader_sampler_count;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_DRAW_COMMAND_H_
