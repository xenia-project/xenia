/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHADER_H_
#define XENIA_GPU_SHADER_H_

#include <string>

#include <xenia/gpu/ucode.h>
#include <xenia/gpu/xenos.h>

namespace xe {
namespace gpu {

class Shader {
 public:
  Shader(ShaderType shader_type, uint64_t data_hash, const uint32_t* dword_ptr,
         uint32_t dword_count);

  ShaderType type() const { return shader_type_; }
  bool is_valid() const { return is_valid_; }
  const std::string& ucode_disassembly() const { return ucode_disassembly_; }
  const std::string& translated_disassembly() const {
    return translated_disassembly_;
  }

  bool Translate();

  struct BufferDescElement {
    ucode::instr_fetch_vtx_t vtx_fetch;
    uint32_t format;
    uint32_t offset_words;
    uint32_t size_words;
    bool is_signed;
    bool is_normalized;
  };
  struct BufferDesc {
    uint32_t input_index;
    uint32_t fetch_slot;
    uint32_t stride_words;
    uint32_t element_count;
    BufferDescElement elements[16];
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
    ucode::instr_fetch_tex_t tex_fetch;
  };
  struct SamplerInputs {
    uint32_t count;
    SamplerDesc descs[32];
  };
  const SamplerInputs& sampler_inputs() { return sampler_inputs_; }

  struct AllocCounts {
    uint32_t positions;
    uint32_t params;
    uint32_t memories;
    bool point_size;
  };
  const AllocCounts& alloc_counts() const { return alloc_counts_; }
  const std::vector<ucode::instr_cf_exec_t>& execs() const { return execs_; }
  const std::vector<ucode::instr_cf_alloc_t>& allocs() const { return allocs_; }

 protected:
  virtual bool TranslateImpl() = 0;

  void GatherIO();
  void GatherAlloc(const ucode::instr_cf_alloc_t* cf);
  void GatherExec(const ucode::instr_cf_exec_t* cf);
  void GatherVertexFetch(const ucode::instr_fetch_vtx_t* vtx);
  void GatherTextureFetch(const ucode::instr_fetch_tex_t* tex);

  ShaderType shader_type_;
  uint64_t data_hash_;
  std::vector<uint32_t> data_;
  bool is_valid_;

  std::string ucode_disassembly_;
  std::string translated_disassembly_;

  AllocCounts alloc_counts_;
  std::vector<ucode::instr_cf_exec_t> execs_;
  std::vector<ucode::instr_cf_alloc_t> allocs_;
  BufferInputs buffer_inputs_;
  SamplerInputs sampler_inputs_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SHADER_H_
