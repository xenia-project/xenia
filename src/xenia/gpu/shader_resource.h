/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHADER_RESOURCE_H_
#define XENIA_GPU_SHADER_RESOURCE_H_

#include <vector>

#include <xenia/gpu/buffer_resource.h>
#include <xenia/gpu/resource.h>
#include <xenia/gpu/xenos/ucode.h>
#include <xenia/gpu/xenos/xenos.h>


namespace xe {
namespace gpu {


class ShaderResource : public HashedResource {
public:
  struct Info {
    // type, etc?
  };

  ~ShaderResource() override;

  const Info& info() const { return info_; }
  xenos::XE_GPU_SHADER_TYPE type() const { return type_; }
  const uint32_t* dwords() const { return dwords_; }
  const size_t dword_count() const { return dword_count_; }

  bool is_prepared() const { return is_prepared_; }
  const char* disasm_src() const { return disasm_src_; }

  struct BufferDesc {
    uint32_t input_index;
    uint32_t fetch_slot;
    VertexBufferResource::Info info;
    // xenos::instr_fetch_vtx_t vtx_fetch; for each el
  };
  struct BufferInputs {
    uint32_t count;
    BufferDesc descs[32];
  };
  const BufferInputs& buffer_inputs() { return buffer_inputs_; }

  struct SamplerDesc {
    uint32_t input_index;
    uint32_t fetch_slot;
    uint32_t format;
    xenos::instr_fetch_tex_t tex_fetch;
  };
  struct SamplerInputs {
    uint32_t count;
    SamplerDesc descs[32];
  };
  const SamplerInputs& sampler_inputs() { return sampler_inputs_; }

  struct AllocCounts {
    uint32_t  positions;
    uint32_t  params;
    uint32_t  memories;
    bool      point_size;
  };
  const AllocCounts& alloc_counts() const { return alloc_counts_; }
  const std::vector<xenos::instr_cf_exec_t>& execs() const { return execs_; }
  const std::vector<xenos::instr_cf_alloc_t>& allocs() const { return allocs_; }

private:
  void GatherIO();
  void GatherAlloc(const xenos::instr_cf_alloc_t* cf);
  void GatherExec(const xenos::instr_cf_exec_t* cf);
  void GatherVertexFetch(const xenos::instr_fetch_vtx_t* vtx);
  void GatherTextureFetch(const xenos::instr_fetch_tex_t* tex);

protected:
  ShaderResource(const MemoryRange& memory_range,
                 const Info& info,
                 xenos::XE_GPU_SHADER_TYPE type);

  Info info_;
  xenos::XE_GPU_SHADER_TYPE type_;
  size_t dword_count_;
  uint32_t* dwords_;
  char* disasm_src_;

  AllocCounts alloc_counts_;
  std::vector<xenos::instr_cf_exec_t> execs_;
  std::vector<xenos::instr_cf_alloc_t> allocs_;
  BufferInputs buffer_inputs_;
  SamplerInputs sampler_inputs_;

  bool is_prepared_;
};


class VertexShaderResource : public ShaderResource {
public:
  VertexShaderResource(const MemoryRange& memory_range,
                       const Info& info);
  ~VertexShaderResource() override;

  // buffer_inputs() matching VertexBufferResource::Info

  virtual int Prepare(const xenos::xe_gpu_program_cntl_t& program_cntl) = 0;
};


class PixelShaderResource : public ShaderResource {
public:
  PixelShaderResource(const MemoryRange& memory_range,
                      const Info& info);
  ~PixelShaderResource() override;

  virtual int Prepare(const xenos::xe_gpu_program_cntl_t& program_cntl,
                      VertexShaderResource* vertex_shader) = 0;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_SHADER_RESOURCE_H_
