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

  const xenos::instr_fetch_vtx_t* GetFetchVtxBySlot(uint32_t fetch_slot);

  // NOTE: xe_free() the returned string!
  char* Disassemble();

private:
  void GatherIO();
  void GatherAlloc(const xenos::instr_cf_alloc_t* cf);
  void GatherExec(const xenos::instr_cf_exec_t* cf);
  void GatherVertexFetch(const xenos::instr_fetch_vtx_t* vtx);

protected:
  xenos::XE_GPU_SHADER_TYPE type_;
  uint32_t*   dwords_;
  size_t      dword_count_;
  uint64_t    hash_;
  bool        is_prepared_;

  std::vector<xenos::instr_cf_alloc_t>  allocs_;
  std::vector<xenos::instr_fetch_vtx_t> fetch_vtxs_;
  xenos::instr_fetch_vtx_t fetch_vtx_slots_[96];
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_SHADER_H_
