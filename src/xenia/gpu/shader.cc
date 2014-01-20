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
    type_(type), hash_(hash), is_prepared_(false), disasm_src_(NULL) {
  xe_zero_struct(&alloc_counts_, sizeof(alloc_counts_));
  xe_zero_struct(&vtx_buffer_inputs_, sizeof(vtx_buffer_inputs_));
  xe_zero_struct(&tex_buffer_inputs_, sizeof(tex_buffer_inputs_));

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

  // Disassemble, for debugging.
  disasm_src_ = DisassembleShader(type_, dwords_, dword_count_);
}

Shader::~Shader() {
  if (disasm_src_) {
    xe_free(disasm_src_);
  }
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

  switch (cf->buffer_select) {
  case SQ_POSITION:
    // Position (SV_POSITION).
    alloc_counts_.positions += cf->size + 1;
    break;
  case SQ_PARAMETER_PIXEL:
    // Output to PS (if VS), or frag output (if PS).
    alloc_counts_.params += cf->size + 1;
    break;
  case SQ_MEMORY:
    // MEMEXPORT?
    alloc_counts_.memories += cf->size + 1;
    break;
  }
}

void Shader::GatherExec(const instr_cf_exec_t* cf) {
  execs_.push_back(*cf);

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
        GatherTextureFetch(&fetch->tex);
        break;
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
      const instr_alu_t* alu =
          (const instr_alu_t*)(dwords_ + alu_off * 3);
      if (alu->vector_write_mask) {
        if (alu->export_data && alu->vector_dest == 63) {
          alloc_counts_.point_size = true;
        }
      }
      if (alu->scalar_write_mask || !alu->vector_write_mask) {
        if (alu->export_data && alu->scalar_dest == 63) {
          alloc_counts_.point_size = true;
        }
      }
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

  // Sometimes games have fetches that just produce constants. We can
  // ignore those.
  uint32_t dst_swiz = vtx->dst_swiz;
  bool fetches_any_data = false;
  for (int i = 0; i < 4; i++) {
    if ((dst_swiz & 0x7) == 4) {
      // 0.0
    } else if ((dst_swiz & 0x7) == 5) {
      // 1.0
    } else if ((dst_swiz & 0x7) == 6) {
      // ?
    } else if ((dst_swiz & 0x7) == 7) {
      // Previous register value.
    } else {
      fetches_any_data = true;
      break;
    }
    dst_swiz >>= 3;
  }
  if (!fetches_any_data) {
    return;
  }

  uint32_t fetch_slot = vtx->const_index * 3 + vtx->const_index_sel;
  auto& inputs = vtx_buffer_inputs_;
  vtx_buffer_element_t* el = NULL;
  for (size_t n = 0; n < inputs.count; n++) {
    auto& input = inputs.descs[n];
    if (input.fetch_slot == fetch_slot) {
      XEASSERT(input.element_count + 1 < XECOUNT(input.elements));
      // It may not hold that all strides are equal, but I hope it does.
      XEASSERT(!vtx->stride || input.stride_words == vtx->stride);
      el = &input.elements[input.element_count++];
      break;
    }
  }
  if (!el) {
    XEASSERTNOTZERO(vtx->stride);
    XEASSERT(inputs.count + 1 < XECOUNT(inputs.descs));
    auto& input = inputs.descs[inputs.count++];
    input.input_index = inputs.count - 1;
    input.fetch_slot = fetch_slot;
    input.stride_words = vtx->stride;
    el = &input.elements[input.element_count++];
  }

  el->vtx_fetch = *vtx;
  el->format = vtx->format;
  el->offset_words = vtx->offset;
  el->size_words = 0;
  switch (el->format) {
  case FMT_8_8_8_8:
  case FMT_2_10_10_10:
  case FMT_10_11_11:
  case FMT_11_11_10:
    el->size_words = 1;
    break;
  case FMT_16_16:
  case FMT_16_16_FLOAT:
    el->size_words = 1;
    break;
  case FMT_16_16_16_16:
  case FMT_16_16_16_16_FLOAT:
    el->size_words = 2;
    break;
  case FMT_32:
  case FMT_32_FLOAT:
    el->size_words = 1;
    break;
  case FMT_32_32:
  case FMT_32_32_FLOAT:
    el->size_words = 2;
    break;
  case FMT_32_32_32_FLOAT:
    el->size_words = 3;
    break;
  case FMT_32_32_32_32:
  case FMT_32_32_32_32_FLOAT:
    el->size_words = 4;
    break;
  default:
    XELOGE("Unknown vertex format: %d", el->format);
    XEASSERTALWAYS();
    break;
  }
}

const Shader::vtx_buffer_inputs_t* Shader::GetVertexBufferInputs() {
  return &vtx_buffer_inputs_;
}

void Shader::GatherTextureFetch(const xenos::instr_fetch_tex_t* tex) {
  // TODO(benvanik): check dest_swiz to see if we are writing anything.

  auto& inputs = tex_buffer_inputs_;
  XEASSERT(inputs.count + 1 < XECOUNT(inputs.descs));
  auto& input = inputs.descs[inputs.count++];
  input.input_index = inputs.count - 1;
  input.fetch_slot = tex->const_idx & 0xF; // ?
  input.tex_fetch = *tex;

  // Format mangling, size estimation, etc.
}

const Shader::tex_buffer_inputs_t* Shader::GetTextureBufferInputs() {
  return &tex_buffer_inputs_;
}
