/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/graphics_driver.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


GraphicsDriver::GraphicsDriver(Memory* memory) :
    memory_(memory), address_translation_(0) {
}

GraphicsDriver::~GraphicsDriver() {
}

int GraphicsDriver::LoadShader(XE_GPU_SHADER_TYPE type,
                               uint32_t address, uint32_t length,
                               uint32_t start) {
  MemoryRange memory_range(
      memory_->Translate(address),
      address, length);

  ShaderResource* shader = nullptr;
  if (type == XE_GPU_SHADER_TYPE_VERTEX) {
    VertexShaderResource::Info info;
    shader = vertex_shader_ = resource_cache()->FetchVertexShader(memory_range,
                                                                  info);
    if (!vertex_shader_) {
      XELOGE("Unable to fetch vertex shader");
      return 1;
    }
  } else {
    PixelShaderResource::Info info;
    shader = pixel_shader_ = resource_cache()->FetchPixelShader(memory_range,
                                                                info);
    if (!pixel_shader_) {
      XELOGE("Unable to fetch pixel shader");
      return 1;
    }
  }

  if (!shader->is_prepared()) {
    // Disassemble.
    const char* source = shader->disasm_src();
    XELOGGPU("Set shader %d at %0.8X (%db):\n%s",
             type, address, length,
             source ? source : "<failed to disassemble>");
  }

  return 0;
}

int GraphicsDriver::PrepareDraw(DrawCommand& command) {
  SCOPE_profile_cpu_f("gpu");

  // Ignore copies for now.
  uint32_t enable_mode = register_file_[XE_GPU_REG_RB_MODECONTROL].u32 & 0x7;
  if (enable_mode != 4) {
    XELOGW("GPU: ignoring draw with enable mode %d", enable_mode);
    return 1;
  }

  // Reset the things we don't modify so that we have clean state.
  command.prim_type = XE_GPU_PRIMITIVE_TYPE_POINT_LIST;
  command.index_count = 0;
  command.index_buffer = nullptr;

  // Generic stuff.
  command.start_index = register_file_[XE_GPU_REG_VGT_INDX_OFFSET].u32;
  command.base_vertex = 0;

  int ret;
  ret = PopulateState(command);
  if (ret) {
    XELOGE("Unable to prepare draw state");
    return ret;
  }
  ret = PopulateConstantBuffers(command);
  if (ret) {
    XELOGE("Unable to prepare draw constant buffers");
    return ret;
  }
  ret = PopulateShaders(command);
  if (ret) {
    XELOGE("Unable to prepare draw shaders");
    return ret;
  }
  ret = PopulateInputAssembly(command);
  if (ret) {
    XELOGE("Unable to prepare draw input assembly");
    return ret;
  }
  ret = PopulateSamplers(command);
  if (ret) {
    XELOGE("Unable to prepare draw samplers");
    return ret;
  }
  return 0;
}

int GraphicsDriver::PrepareDrawIndexBuffer(
    DrawCommand& command,
    uint32_t address, uint32_t length,
    xenos::XE_GPU_ENDIAN endianness,
    IndexFormat format) {
  SCOPE_profile_cpu_f("gpu");

  address += address_translation_;
  MemoryRange memory_range(memory_->Translate(address), address, length);

  IndexBufferResource::Info info;
  info.endianness = endianness;
  info.format = format;

  command.index_buffer =
      resource_cache()->FetchIndexBuffer(memory_range, info);
  if (!command.index_buffer) {
    return 1;
  }
  return 0;
}

int GraphicsDriver::PopulateState(DrawCommand& command) {
  return 0;
}

int GraphicsDriver::PopulateConstantBuffers(DrawCommand& command) {
  command.float4_constants.count = 512;
  command.float4_constants.values =
      &register_file_[XE_GPU_REG_SHADER_CONSTANT_000_X].f32;
  command.loop_constants.count = 32;
  command.loop_constants.values =
      &register_file_[XE_GPU_REG_SHADER_CONSTANT_LOOP_00].u32;
  command.bool_constants.count = 8;
  command.bool_constants.values =
      &register_file_[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031].u32;
  return 0;
}

int GraphicsDriver::PopulateShaders(DrawCommand& command) {
  SCOPE_profile_cpu_f("gpu");

  if (!vertex_shader_) {
    XELOGE("No vertex shader bound; ignoring");
    return 1;
  }
  if (!pixel_shader_) {
    XELOGE("No pixel shader bound; ignoring");
    return 1;
  }

  xe_gpu_program_cntl_t program_cntl;
  program_cntl.dword_0 = register_file_[XE_GPU_REG_SQ_PROGRAM_CNTL].u32;
  if (!vertex_shader_->is_prepared()) {
    if (vertex_shader_->Prepare(program_cntl)) {
      XELOGE("Unable to prepare vertex shader");
      return 1;
    }
  }
  if (!pixel_shader_->is_prepared()) {
    if (pixel_shader_->Prepare(program_cntl, vertex_shader_)) {
      XELOGE("Unable to prepare pixel shader");
      return 1;
    }
  }

  command.vertex_shader = vertex_shader_;
  command.pixel_shader = pixel_shader_;

  return 0;
}

int GraphicsDriver::PopulateInputAssembly(DrawCommand& command) {
  SCOPE_profile_cpu_f("gpu");

  const auto& buffer_inputs = command.vertex_shader->buffer_inputs();
  command.vertex_buffer_count = buffer_inputs.count;
  for (size_t n = 0; n < buffer_inputs.count; n++) {
    const auto& desc = buffer_inputs.descs[n];

    int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + (desc.fetch_slot / 3) * 6;
    auto group = reinterpret_cast<xe_gpu_fetch_group_t*>(&register_file_.values[r]);
    xe_gpu_vertex_fetch_t* fetch = nullptr;
    switch (desc.fetch_slot % 3) {
    case 0:
      fetch = &group->vertex_fetch_0;
      break;
    case 1:
      fetch = &group->vertex_fetch_1;
      break;
    case 2:
      fetch = &group->vertex_fetch_2;
      break;
    }
    assert_not_null(fetch);
    // If this assert doesn't hold, maybe we just abort?
    assert_true(fetch->type == 0x3);
    assert_not_zero(fetch->size);

    const auto& info = desc.info;

    MemoryRange memory_range;
    memory_range.guest_base = (fetch->address << 2) + address_translation_;
    memory_range.host_base = memory_->Translate(memory_range.guest_base);
    memory_range.length = fetch->size * 4;
    // TODO(benvanik): if the memory range is within the command buffer, we
    //     should use a cached transient buffer.

    auto buffer = resource_cache()->FetchVertexBuffer(memory_range, info);
    if (!buffer) {
      XELOGE("Unable to create vertex fetch buffer");
      return 1;
    }

    command.vertex_buffers[n].input_index = desc.input_index;
    command.vertex_buffers[n].buffer = buffer;
    command.vertex_buffers[n].stride = desc.info.stride_words * 4;
    command.vertex_buffers[n].offset = 0;
  }
  return 0;
}

int GraphicsDriver::PopulateSamplers(DrawCommand& command) {
  SCOPE_profile_cpu_f("gpu");

  // Vertex texture samplers.
  const auto& vertex_sampler_inputs = command.vertex_shader->sampler_inputs();
  command.vertex_shader_sampler_count = vertex_sampler_inputs.count;
  for (size_t i = 0; i < command.vertex_shader_sampler_count; ++i) {
    if (PopulateSamplerSet(vertex_sampler_inputs.descs[i],
                           command.vertex_shader_samplers[i])) {
      return 1;
    }
  }

  // Pixel shader texture sampler.
  const auto& pixel_sampler_inputs = command.pixel_shader->sampler_inputs();
  command.pixel_shader_sampler_count = pixel_sampler_inputs.count;
  for (size_t i = 0; i < command.pixel_shader_sampler_count; ++i) {
    if (PopulateSamplerSet(pixel_sampler_inputs.descs[i],
                           command.pixel_shader_samplers[i])) {
      return 1;
    }
  }

  return 0;
}

int GraphicsDriver::PopulateSamplerSet(
    const ShaderResource::SamplerDesc& src_input,
    DrawCommand::SamplerInput& dst_input) {
  int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + src_input.fetch_slot * 6;
  const auto group = (const xe_gpu_fetch_group_t*)&register_file_.values[r];
  const xenos::xe_gpu_texture_fetch_t& fetch = group->texture_fetch;
  if (fetch.type != 0x2) {
    return 0;
  }

  dst_input.input_index = src_input.input_index;
  dst_input.texture = nullptr;
  dst_input.sampler_state = nullptr;

  TextureResource::Info info;
  if (!TextureResource::Info::Prepare(fetch, info)) {
    XELOGE("D3D11: unable to parse texture fetcher info");
    return 0;  // invalid texture used
  }
  if (info.format == DXGI_FORMAT_UNKNOWN) {
    XELOGW("D3D11: unknown texture format %d", info.format);
    return 0;  // invalid texture used
  }

  // TODO(benvanik): quick validate without refetching intraframe.
  // Fetch texture from the cache.
  MemoryRange memory_range;
  memory_range.guest_base = (fetch.address << 12) + address_translation_;
  memory_range.host_base = memory_->Translate(memory_range.guest_base);
  memory_range.length = info.input_length;

  auto texture = resource_cache()->FetchTexture(memory_range, info);
  if (!texture) {
    XELOGW("D3D11: unable to fetch texture");
    return 0;  // invalid texture used
  }

  SamplerStateResource::Info sampler_info;
  if (!SamplerStateResource::Info::Prepare(fetch,
                                           src_input.tex_fetch,
                                           sampler_info)) {
    XELOGW("D3D11: unable to parse sampler info");
    return 0;  // invalid texture used
  }
  auto sampler_state = resource_cache()->FetchSamplerState(sampler_info);
  if (!sampler_state) {
    XELOGW("D3D11: unable to fetch sampler");
    return 0;  // invalid texture used
  }

  dst_input.texture = texture;
  dst_input.sampler_state = sampler_state;
  return 0;
}
