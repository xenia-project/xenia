/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHADER_H_
#define XENIA_GPU_SHADER_H_

#include <xenia/core.h>
#include <xenia/gpu/xenos/ucode.h>
#include <xenia/gpu/xenos/xenos.h>


namespace xe {
namespace gpu {


class Shader {
public:
  Shader(xenos::XE_GPU_SHADER_TYPE type,
         const uint8_t* src_ptr, size_t length,
         uint64_t hash);
  virtual ~Shader();

  xenos::XE_GPU_SHADER_TYPE type() const { return type_; }
  const uint32_t* dwords() const { return dwords_; }
  size_t dword_count() const { return dword_count_; }
  uint64_t hash() const { return hash_; }
  bool is_prepared() const { return is_prepared_; }

  const char* disasm_src() const { return disasm_src_; }

  typedef struct {
    xenos::instr_fetch_vtx_t vtx_fetch;
    uint32_t format;
    uint32_t offset_words;
    uint32_t size_words;
  } vtx_buffer_element_t;
  typedef struct {
    uint32_t input_index;
    uint32_t fetch_slot;
    uint32_t stride_words;
    uint32_t element_count;
    vtx_buffer_element_t elements[16];
  } vtx_buffer_desc_t;
  typedef struct {
    uint32_t count;
    vtx_buffer_desc_t descs[16];
  } vtx_buffer_inputs_t;
  const vtx_buffer_inputs_t* GetVertexBufferInputs();

  typedef struct {
    uint32_t input_index;
    uint32_t fetch_slot;
    xenos::instr_fetch_tex_t tex_fetch;
    uint32_t format;
  } tex_buffer_desc_t;
  typedef struct {
    uint32_t count;
    tex_buffer_desc_t descs[32];
  } tex_buffer_inputs_t;
  const tex_buffer_inputs_t* GetTextureBufferInputs();

  typedef struct {
    uint32_t  positions;
    uint32_t  params;
    uint32_t  memories;
    bool      point_size;
  } alloc_counts_t;
  const alloc_counts_t& alloc_counts() const { return alloc_counts_; }

private:
  void GatherIO();
  void GatherAlloc(const xenos::instr_cf_alloc_t* cf);
  void GatherExec(const xenos::instr_cf_exec_t* cf);
  void GatherVertexFetch(const xenos::instr_fetch_vtx_t* vtx);
  void GatherTextureFetch(const xenos::instr_fetch_tex_t* tex);

protected:
  xenos::XE_GPU_SHADER_TYPE type_;
  uint32_t*   dwords_;
  size_t      dword_count_;
  uint64_t    hash_;
  bool        is_prepared_;

  char*       disasm_src_;

  alloc_counts_t alloc_counts_;
  std::vector<xenos::instr_cf_exec_t>   execs_;
  std::vector<xenos::instr_cf_alloc_t>  allocs_;
  vtx_buffer_inputs_t vtx_buffer_inputs_;
  tex_buffer_inputs_t tex_buffer_inputs_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_SHADER_H_
