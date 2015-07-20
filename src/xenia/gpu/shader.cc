/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/shader.h"

#include <cstring>

#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/gpu/ucode_disassembler.h"

namespace xe {
namespace gpu {

using namespace xe::gpu::ucode;
using namespace xe::gpu::xenos;

Shader::Shader(ShaderType shader_type, uint64_t data_hash,
               const uint32_t* dword_ptr, uint32_t dword_count)
    : shader_type_(shader_type),
      data_hash_(data_hash),
      has_prepared_(false),
      is_valid_(false) {
  data_.resize(dword_count);
  xe::copy_and_swap(data_.data(), dword_ptr, dword_count);
  std::memset(&alloc_counts_, 0, sizeof(alloc_counts_));
  std::memset(&buffer_inputs_, 0, sizeof(buffer_inputs_));
  std::memset(&sampler_inputs_, 0, sizeof(sampler_inputs_));

  // Disassemble ucode and stash.
  // TODO(benvanik): debug only.
  ucode_disassembly_ =
      DisassembleShader(shader_type_, data_.data(), data_.size());

  // Gather input/output registers/etc.
  GatherIO();
}

Shader::~Shader() = default;

void Shader::GatherIO() {
  // Process all execution blocks.
  instr_cf_t cfa;
  instr_cf_t cfb;
  for (size_t idx = 0; idx < data_.size(); idx += 3) {
    uint32_t dword_0 = data_[idx + 0];
    uint32_t dword_1 = data_[idx + 1];
    uint32_t dword_2 = data_[idx + 2];
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
  uint32_t sequence = cf->serialize;
  for (uint32_t i = 0; i < cf->count; i++) {
    uint32_t alu_off = (cf->address + i);
    // int sync = sequence & 0x2;
    if (sequence & 0x1) {
      auto fetch =
          reinterpret_cast<const instr_fetch_t*>(data_.data() + alu_off * 3);
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
          assert_always();
          break;
      }
    } else {
      // TODO(benvanik): gather registers used, predicate bits used, etc.
      auto alu =
          reinterpret_cast<const instr_alu_t*>(data_.data() + alu_off * 3);
      if (alu->export_data && alu->vector_write_mask) {
        switch (alu->vector_dest) {
          case 0:
          case 1:
          case 2:
          case 3:
            alloc_counts_.color_targets[alu->vector_dest] = true;
            break;
          case 63:
            alloc_counts_.point_size = true;
            break;
        }
      }
      if (alu->export_data &&
          (alu->scalar_write_mask || !alu->vector_write_mask)) {
        switch (alu->scalar_dest) {
          case 0:
          case 1:
          case 2:
          case 3:
            alloc_counts_.color_targets[alu->scalar_dest] = true;
            break;
          case 63:
            alloc_counts_.point_size = true;
            break;
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

  if (!vtx->must_be_one) {
    return;
  }

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

  assert_true(vtx->const_index <= 0x1F);

  uint32_t fetch_slot = vtx->const_index * 3 + vtx->const_index_sel;
  auto& inputs = buffer_inputs_;
  BufferDescElement* el = nullptr;
  for (size_t n = 0; n < inputs.count; n++) {
    auto& desc = inputs.descs[n];
    if (desc.fetch_slot == fetch_slot) {
      assert_true(desc.element_count <= xe::countof(desc.elements));
      // It may not hold that all strides are equal, but I hope it does.
      assert_true(!vtx->stride || desc.stride_words == vtx->stride);
      el = &desc.elements[desc.element_count++];
      break;
    }
  }
  if (!el) {
    assert_not_zero(vtx->stride);
    assert_true(inputs.count + 1 < xe::countof(inputs.descs));
    auto& desc = inputs.descs[inputs.count++];
    desc.input_index = inputs.count - 1;
    desc.fetch_slot = fetch_slot;
    desc.stride_words = vtx->stride;
    el = &desc.elements[desc.element_count++];
  }
  ++inputs.total_elements_count;

  el->vtx_fetch = *vtx;
  el->format = static_cast<VertexFormat>(vtx->format);
  el->is_normalized = vtx->num_format_all == 0;
  el->is_signed = vtx->format_comp_all == 1;
  el->offset_words = vtx->offset;
  el->size_words = 0;
  switch (el->format) {
    case VertexFormat::k_8_8_8_8:
    case VertexFormat::k_2_10_10_10:
    case VertexFormat::k_10_11_11:
    case VertexFormat::k_11_11_10:
      el->size_words = 1;
      break;
    case VertexFormat::k_16_16:
    case VertexFormat::k_16_16_FLOAT:
      el->size_words = 1;
      break;
    case VertexFormat::k_16_16_16_16:
    case VertexFormat::k_16_16_16_16_FLOAT:
      el->size_words = 2;
      break;
    case VertexFormat::k_32:
    case VertexFormat::k_32_FLOAT:
      el->size_words = 1;
      break;
    case VertexFormat::k_32_32:
    case VertexFormat::k_32_32_FLOAT:
      el->size_words = 2;
      break;
    case VertexFormat::k_32_32_32_FLOAT:
      el->size_words = 3;
      break;
    case VertexFormat::k_32_32_32_32:
    case VertexFormat::k_32_32_32_32_FLOAT:
      el->size_words = 4;
      break;
    default:
      assert_unhandled_case(el->format);
      break;
  }
}

void Shader::GatherTextureFetch(const instr_fetch_tex_t* tex) {
  // TODO(benvanik): check dest_swiz to see if we are writing anything.

  assert_true(tex->const_idx < 0x1F);

  assert_true(sampler_inputs_.count + 1 <= xe::countof(sampler_inputs_.descs));
  auto& input = sampler_inputs_.descs[sampler_inputs_.count++];
  input.input_index = sampler_inputs_.count - 1;
  input.fetch_slot = tex->const_idx & 0xF;  // ??????????????????????????????
  input.tex_fetch = *tex;

  // Format mangling, size estimation, etc.
}

}  //  namespace gpu
}  //  namespace xe
