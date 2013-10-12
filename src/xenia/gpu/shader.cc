/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/shader.h>

#include <xenia/gpu/xenos/ucode_disassembler.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


Shader::Shader(
    XE_GPU_SHADER_TYPE type,
    const uint8_t* src_ptr, size_t length,
    uint64_t hash) :
    type_(type), hash_(hash), is_prepared_(false) {
  xe_zero_struct(fetch_vtx_slots_, sizeof(fetch_vtx_slots_));

  // Verify.
  dword_count_ = length / 4;
  XEASSERT(dword_count_ <= 512);

  // Copy bytes and swap.
  size_t byte_size = dword_count_ * sizeof(uint32_t);
  dwords_ = (uint32_t*)xe_malloc(byte_size);
  for (uint32_t n = 0; n < dword_count_; n++) {
    dwords_[n] = XEGETUINT32BE(src_ptr + n * 4);
  }

  // Gather input/output registers/etc.
  GatherIO();
}

Shader::~Shader() {
  xe_free(dwords_);
}

void Shader::GatherIO() {
  // Process all execution blocks.
  instr_cf_t cfa;
  instr_cf_t cfb;
  for (int idx = 0; idx < dword_count_; idx += 3) {
    uint32_t dword_0 = dwords_[idx + 0];
    uint32_t dword_1 = dwords_[idx + 1];
    uint32_t dword_2 = dwords_[idx + 2];
    cfa.dword_0 = dword_0;
    cfa.dword_1 = dword_1 & 0xFFFF;
    cfb.dword_0 = (dword_1 >> 16) | (dword_2 << 16);
    cfb.dword_1 = dword_2 >> 16;
    if (cfa.opc == ALLOC) {
      GatherAlloc(&cfa.alloc);
    } else if (cfa.is_exec()) {
      GatherExec(&cfa.exec);
    }
    if (cfb.opc == ALLOC) {
      GatherAlloc(&cfb.alloc);
    } else if (cfb.is_exec()) {
      GatherExec(&cfb.exec);
    }
    if (cfa.opc == EXEC_END || cfb.opc == EXEC_END) {
      break;
    }
  }
}

void Shader::GatherAlloc(const instr_cf_alloc_t* cf) {
  allocs_.push_back(*cf);
}

void Shader::GatherExec(const instr_cf_exec_t* cf) {
  uint32_t sequence = cf->serialize;
  for (uint32_t i = 0; i < cf->count; i++) {
    uint32_t alu_off = (cf->address + i);
    int sync = sequence & 0x2;
    if (sequence & 0x1) {
      const instr_fetch_t* fetch =
          (const instr_fetch_t*)(dwords_ + alu_off * 3);
      switch (fetch->opc) {
      case VTX_FETCH:
        GatherVertexFetch(&fetch->vtx);
        break;
      case TEX_FETCH:
      case TEX_GET_BORDER_COLOR_FRAC:
      case TEX_GET_COMP_TEX_LOD:
      case TEX_GET_GRADIENTS:
      case TEX_GET_WEIGHTS:
      case TEX_SET_TEX_LOD:
      case TEX_SET_GRADIENTS_H:
      case TEX_SET_GRADIENTS_V:
      default:
        XEASSERTALWAYS();
        break;
      }
    } else {
      // TODO(benvanik): gather registers used, predicate bits used, etc.
      /*const instr_alu_t* alu =
          (const instr_alu_t*)(dwords_ + alu_off * 3);*/
    }
    sequence >>= 2;
  }
}

void Shader::GatherVertexFetch(const instr_fetch_vtx_t* vtx) {
  // dst_reg/dst_swiz
  // src_reg/src_swiz
  // format = a2xx_sq_surfaceformat
  // format_comp_all ? signed : unsigned
  // num_format_all ? normalized
  // stride
  // offset
  // const_index/const_index_sel -- fetch constant register
  // num_format_all ? integer : fraction
  // exp_adjust_all - [-32,31] - (2^exp_adjust_all)*fetch - 0 = default

  fetch_vtxs_.push_back(*vtx);

  uint32_t fetch_slot = vtx->const_index * 3 + vtx->const_index_sel;
  fetch_vtx_slots_[fetch_slot] = *vtx;
}

const instr_fetch_vtx_t* Shader::GetFetchVtxBySlot(uint32_t fetch_slot) {
  return &fetch_vtx_slots_[fetch_slot];
}

char* Shader::Disassemble() {
  return DisassembleShader(type_, dwords_, dword_count_);
}
