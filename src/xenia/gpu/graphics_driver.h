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
#include <xenia/gpu/draw_command.h>
#include <xenia/gpu/register_file.h>
#include <xenia/gpu/resource_cache.h>
#include <xenia/gpu/xenos/xenos.h>


namespace xe {
namespace gpu {


class GraphicsDriver {
public:
  virtual ~GraphicsDriver();

  Memory* memory() const { return memory_; }
  virtual ResourceCache* resource_cache() const = 0;
  RegisterFile* register_file() { return &register_file_; };
  void set_address_translation(uint32_t value) {
    address_translation_ = value;
  }

  virtual int Initialize() = 0;

  int LoadShader(xenos::XE_GPU_SHADER_TYPE type,
                 uint32_t address, uint32_t length, 
                 uint32_t start);

  int PrepareDraw(DrawCommand& command);
  int PrepareDrawIndexBuffer(DrawCommand& command,
                             uint32_t address, uint32_t length,
                             xenos::XE_GPU_ENDIAN endianness,
                             IndexFormat format);
  virtual int Draw(const DrawCommand& command) = 0;

  virtual int Resolve() = 0;

private:
  int PopulateState(DrawCommand& command);
  int PopulateConstantBuffers(DrawCommand& command);
  int PopulateShaders(DrawCommand& command);
  int PopulateInputAssembly(DrawCommand& command);
  int PopulateSamplers(DrawCommand& command);
  int PopulateSamplerSet(const ShaderResource::SamplerDesc& src_input,
                         DrawCommand::SamplerInput& dst_input);

protected:
  GraphicsDriver(Memory* memory);

  Memory* memory_;
  RegisterFile register_file_;
  uint32_t address_translation_;

  VertexShaderResource* vertex_shader_;
  PixelShaderResource* pixel_shader_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_GRAPHICS_DRIVER_H_
