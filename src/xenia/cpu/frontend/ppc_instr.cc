/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/frontend/ppc_instr.h"

#include <cinttypes>
#include <sstream>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/base/string_buffer.h"
#include "xenia/cpu/frontend/ppc_instr_tables.h"

namespace xe {
namespace cpu {
namespace frontend {

std::vector<InstrType*> all_instrs_;

void DumpAllInstrCounts() {
  StringBuffer sb;
  sb.Append("Instruction translation counts:\n");
  for (auto instr_type : all_instrs_) {
    if (instr_type->translation_count) {
      sb.AppendFormat("%8d : %s\n", instr_type->translation_count,
                      instr_type->name);
    }
  }
  fprintf(stdout, "%s", sb.GetString());
  fflush(stdout);
}

void InstrOperand::Dump(std::string& out_str) {
  if (display) {
    out_str += display;
    return;
  }

  char buffer[32];
  const size_t max_count = xe::countof(buffer);
  switch (type) {
    case InstrOperand::kRegister:
      switch (reg.set) {
        case InstrRegister::kXER:
          snprintf(buffer, max_count, "XER");
          break;
        case InstrRegister::kLR:
          snprintf(buffer, max_count, "LR");
          break;
        case InstrRegister::kCTR:
          snprintf(buffer, max_count, "CTR");
          break;
        case InstrRegister::kCR:
          snprintf(buffer, max_count, "CR%d", reg.ordinal);
          break;
        case InstrRegister::kFPSCR:
          snprintf(buffer, max_count, "FPSCR");
          break;
        case InstrRegister::kGPR:
          snprintf(buffer, max_count, "r%d", reg.ordinal);
          break;
        case InstrRegister::kFPR:
          snprintf(buffer, max_count, "f%d", reg.ordinal);
          break;
        case InstrRegister::kVMX:
          snprintf(buffer, max_count, "vr%d", reg.ordinal);
          break;
      }
      break;
    case InstrOperand::kImmediate:
      switch (imm.width) {
        case 1:
          if (imm.is_signed) {
            snprintf(buffer, max_count, "%d", (int32_t)(int8_t)imm.value);
          } else {
            snprintf(buffer, max_count, "0x%.2X", (uint8_t)imm.value);
          }
          break;
        case 2:
          if (imm.is_signed) {
            snprintf(buffer, max_count, "%d", (int32_t)(int16_t)imm.value);
          } else {
            snprintf(buffer, max_count, "0x%.4X", (uint16_t)imm.value);
          }
          break;
        case 4:
          if (imm.is_signed) {
            snprintf(buffer, max_count, "%d", (int32_t)imm.value);
          } else {
            snprintf(buffer, max_count, "0x%.8X", (uint32_t)imm.value);
          }
          break;
        case 8:
          if (imm.is_signed) {
            snprintf(buffer, max_count, "%" PRId64, (int64_t)imm.value);
          } else {
            snprintf(buffer, max_count, "0x%.16" PRIX64, imm.value);
          }
          break;
      }
      break;
  }
  out_str += buffer;
}

void InstrAccessBits::Clear() { spr = cr = gpr = fpr = 0; }

void InstrAccessBits::Extend(InstrAccessBits& other) {
  spr |= other.spr;
  cr |= other.cr;
  gpr |= other.gpr;
  fpr |= other.fpr;
  vr31_0 |= other.vr31_0;
  vr63_32 |= other.vr63_32;
  vr95_64 |= other.vr95_64;
  vr127_96 |= other.vr127_96;
}

void InstrAccessBits::MarkAccess(InstrRegister& reg) {
  uint64_t bits = 0;
  if (reg.access & InstrRegister::kRead) {
    bits |= 0x1;
  }
  if (reg.access & InstrRegister::kWrite) {
    bits |= 0x2;
  }

  switch (reg.set) {
    case InstrRegister::kXER:
      spr |= bits << (2 * 0);
      break;
    case InstrRegister::kLR:
      spr |= bits << (2 * 1);
      break;
    case InstrRegister::kCTR:
      spr |= bits << (2 * 2);
      break;
    case InstrRegister::kCR:
      cr |= bits << (2 * reg.ordinal);
      break;
    case InstrRegister::kFPSCR:
      spr |= bits << (2 * 3);
      break;
    case InstrRegister::kGPR:
      gpr |= bits << (2 * reg.ordinal);
      break;
    case InstrRegister::kFPR:
      fpr |= bits << (2 * reg.ordinal);
      break;
    case InstrRegister::kVMX:
      if (reg.ordinal < 32) {
        vr31_0 |= bits << (2 * reg.ordinal);
      } else if (reg.ordinal < 64) {
        vr63_32 |= bits << (2 * (reg.ordinal - 32));
      } else if (reg.ordinal < 96) {
        vr95_64 |= bits << (2 * (reg.ordinal - 64));
      } else {
        vr127_96 |= bits << (2 * (reg.ordinal - 96));
      }
      break;
    default:
      assert_unhandled_case(reg.set);
      break;
  }
}

void InstrAccessBits::Dump(std::string& out_str) {
  std::stringstream str;
  if (spr) {
    uint64_t spr_t = spr;
    if (spr_t & 0x3) {
      str << "XER [";
      str << ((spr_t & 1) ? "R" : " ");
      str << ((spr_t & 2) ? "W" : " ");
      str << "] ";
    }
    spr_t >>= 2;
    if (spr_t & 0x3) {
      str << "LR [";
      str << ((spr_t & 1) ? "R" : " ");
      str << ((spr_t & 2) ? "W" : " ");
      str << "] ";
    }
    spr_t >>= 2;
    if (spr_t & 0x3) {
      str << "CTR [";
      str << ((spr_t & 1) ? "R" : " ");
      str << ((spr_t & 2) ? "W" : " ");
      str << "] ";
    }
    spr_t >>= 2;
    if (spr_t & 0x3) {
      str << "FPCSR [";
      str << ((spr_t & 1) ? "R" : " ");
      str << ((spr_t & 2) ? "W" : " ");
      str << "] ";
    }
    spr_t >>= 2;
  }

  if (cr) {
    uint64_t cr_t = cr;
    for (size_t n = 0; n < 8; n++) {
      if (cr_t & 0x3) {
        str << "cr" << n << " [";
        str << ((cr_t & 1) ? "R" : " ");
        str << ((cr_t & 2) ? "W" : " ");
        str << "] ";
      }
      cr_t >>= 2;
    }
  }

  if (gpr) {
    uint64_t gpr_t = gpr;
    for (size_t n = 0; n < 32; n++) {
      if (gpr_t & 0x3) {
        str << "r" << n << " [";
        str << ((gpr_t & 1) ? "R" : " ");
        str << ((gpr_t & 2) ? "W" : " ");
        str << "] ";
      }
      gpr_t >>= 2;
    }
  }

  if (fpr) {
    uint64_t fpr_t = fpr;
    for (size_t n = 0; n < 32; n++) {
      if (fpr_t & 0x3) {
        str << "f" << n << " [";
        str << ((fpr_t & 1) ? "R" : " ");
        str << ((fpr_t & 2) ? "W" : " ");
        str << "] ";
      }
      fpr_t >>= 2;
    }
  }

  if (vr31_0) {
    uint64_t vr31_0_t = vr31_0;
    for (size_t n = 0; n < 32; n++) {
      if (vr31_0_t & 0x3) {
        str << "vr" << n << " [";
        str << ((vr31_0_t & 1) ? "R" : " ");
        str << ((vr31_0_t & 2) ? "W" : " ");
        str << "] ";
      }
      vr31_0_t >>= 2;
    }
  }
  if (vr63_32) {
    uint64_t vr63_32_t = vr63_32;
    for (size_t n = 0; n < 32; n++) {
      if (vr63_32_t & 0x3) {
        str << "vr" << (n + 32) << " [";
        str << ((vr63_32_t & 1) ? "R" : " ");
        str << ((vr63_32_t & 2) ? "W" : " ");
        str << "] ";
      }
      vr63_32_t >>= 2;
    }
  }
  if (vr95_64) {
    uint64_t vr95_64_t = vr95_64;
    for (size_t n = 0; n < 32; n++) {
      if (vr95_64_t & 0x3) {
        str << "vr" << (n + 64) << " [";
        str << ((vr95_64_t & 1) ? "R" : " ");
        str << ((vr95_64_t & 2) ? "W" : " ");
        str << "] ";
      }
      vr95_64_t >>= 2;
    }
  }
  if (vr127_96) {
    uint64_t vr127_96_t = vr127_96;
    for (size_t n = 0; n < 32; n++) {
      if (vr127_96_t & 0x3) {
        str << "vr" << (n + 96) << " [";
        str << ((vr127_96_t & 1) ? "R" : " ");
        str << ((vr127_96_t & 2) ? "W" : " ");
        str << "] ";
      }
      vr127_96_t >>= 2;
    }
  }

  out_str = str.str();
}

void InstrDisasm::Init(const char* new_name, const char* new_info,
                       uint32_t new_flags) {
  name = new_name;
  info = new_info;
  flags = new_flags;
}

void InstrDisasm::AddLR(InstrRegister::Access access) {}

void InstrDisasm::AddCTR(InstrRegister::Access access) {}

void InstrDisasm::AddCR(uint32_t bf, InstrRegister::Access access) {}

void InstrDisasm::AddFPSCR(InstrRegister::Access access) {}

void InstrDisasm::AddRegOperand(InstrRegister::RegisterSet set,
                                uint32_t ordinal, InstrRegister::Access access,
                                const char* display) {}

void InstrDisasm::AddSImmOperand(uint64_t value, size_t width,
                                 const char* display) {}

void InstrDisasm::AddUImmOperand(uint64_t value, size_t width,
                                 const char* display) {}

int InstrDisasm::Finish() { return 0; }

void InstrDisasm::Dump(std::string& out_str, size_t pad) {
  out_str = name;
  if (flags & InstrDisasm::kOE) {
    out_str += "o";
  }
  if (flags & InstrDisasm::kRc) {
    out_str += ".";
  }
  if (flags & InstrDisasm::kLR) {
    out_str += "l";
  }
}

InstrType* GetInstrType(uint32_t code) {
  // Fast lookup via tables.
  InstrType* slot = NULL;
  switch (code >> 26) {
    case 4:
      // Opcode = 4, index = bits 10-0 (10)
      slot = tables::instr_table_4[select_bits(code, 0, 10)];
      break;
    case 19:
      // Opcode = 19, index = bits 10-1 (10)
      slot = tables::instr_table_19[select_bits(code, 1, 10)];
      break;
    case 30:
      // Opcode = 30, index = bits 4-1 (4)
      // Special cased to an uber instruction.
      slot = tables::instr_table_30[select_bits(code, 0, 0)];
      break;
    case 31:
      // Opcode = 31, index = bits 10-1 (10)
      slot = tables::instr_table_31[select_bits(code, 1, 10)];
      break;
    case 58:
      // Opcode = 58, index = bits 1-0 (2)
      slot = tables::instr_table_58[select_bits(code, 0, 1)];
      break;
    case 59:
      // Opcode = 59, index = bits 5-1 (5)
      slot = tables::instr_table_59[select_bits(code, 1, 5)];
      break;
    case 62:
      // Opcode = 62, index = bits 1-0 (2)
      slot = tables::instr_table_62[select_bits(code, 0, 1)];
      break;
    case 63:
      // Opcode = 63, index = bits 10-1 (10)
      slot = tables::instr_table_63[select_bits(code, 1, 10)];
      break;
    default:
      slot = tables::instr_table[select_bits(code, 26, 31)];
      break;
  }
  if (slot && slot->opcode) {
    return slot;
  }

  // Slow lookup via linear scan.
  // This is primarily due to laziness. It could be made fast like the others.
  for (size_t n = 0; n < xe::countof(tables::instr_table_scan); n++) {
    slot = &(tables::instr_table_scan[n]);
    if (slot->opcode == (code & slot->opcode_mask)) {
      return slot;
    }
  }

  return NULL;
}

int RegisterInstrEmit(uint32_t code, InstrEmitFn emit) {
  InstrType* instr_type = GetInstrType(code);
  assert_not_null(instr_type);
  if (!instr_type) {
    return 1;
  }
  all_instrs_.push_back(instr_type);
  assert_null(instr_type->emit);
  instr_type->emit = emit;
  return 0;
}

}  // namespace frontend
}  // namespace cpu
}  // namespace xe
