/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/ppc/ppc_instr.h>

#include <sstream>

#include <alloy/frontend/ppc/ppc_instr_tables.h>


using namespace alloy;
using namespace alloy::frontend;
using namespace alloy::frontend::ppc;


void InstrOperand::Dump(std::string& out_str) {
  if (display) {
    out_str += display;
    return;
  }

  char buffer[32];
  const size_t max_count = XECOUNT(buffer);
  switch (type) {
    case InstrOperand::kRegister:
      switch (reg.set) {
        case InstrRegister::kXER:
          xesnprintfa(buffer, max_count, "XER");
          break;
        case InstrRegister::kLR:
          xesnprintfa(buffer, max_count, "LR");
          break;
        case InstrRegister::kCTR:
          xesnprintfa(buffer, max_count, "CTR");
          break;
        case InstrRegister::kCR:
          xesnprintfa(buffer, max_count, "CR%d", reg.ordinal);
          break;
        case InstrRegister::kFPSCR:
          xesnprintfa(buffer, max_count, "FPSCR");
          break;
        case InstrRegister::kGPR:
          xesnprintfa(buffer, max_count, "r%d", reg.ordinal);
          break;
        case InstrRegister::kFPR:
          xesnprintfa(buffer, max_count, "f%d", reg.ordinal);
          break;
        case InstrRegister::kVMX:
          xesnprintfa(buffer, max_count, "vr%d", reg.ordinal);
          break;
      }
      break;
    case InstrOperand::kImmediate:
      switch (imm.width) {
        case 1:
          if (imm.is_signed) {
            xesnprintfa(buffer, max_count, "%d", (int32_t)(int8_t)imm.value);
          } else {
            xesnprintfa(buffer, max_count, "0x%.2X", (uint8_t)imm.value);
          }
          break;
        case 2:
          if (imm.is_signed) {
            xesnprintfa(buffer, max_count, "%d", (int32_t)(int16_t)imm.value);
          } else {
            xesnprintfa(buffer, max_count, "0x%.4X", (uint16_t)imm.value);
          }
          break;
        case 4:
          if (imm.is_signed) {
            xesnprintfa(buffer, max_count, "%d", (int32_t)imm.value);
          } else {
            xesnprintfa(buffer, max_count, "0x%.8X", (uint32_t)imm.value);
          }
          break;
        case 8:
          if (imm.is_signed) {
            xesnprintfa(buffer, max_count, "%lld", (int64_t)imm.value);
          } else {
            xesnprintfa(buffer, max_count, "0x%.16llX", imm.value);
          }
          break;
      }
      break;
  }
  out_str += buffer;
}


void InstrAccessBits::Clear() {
  spr = cr = gpr = fpr = 0;
}

void InstrAccessBits::Extend(InstrAccessBits& other) {
    spr |= other.spr;
    cr  |= other.cr;
    gpr |= other.gpr;
    fpr |= other.fpr;
    vr31_0    |= other.vr31_0;
    vr63_32   |= other.vr63_32;
    vr95_64   |= other.vr95_64;
    vr127_96  |= other.vr127_96;
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
      cr  |= bits << (2 * reg.ordinal);
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
      XEASSERTALWAYS();
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


void InstrDisasm::Init(const char* name, const char* info, uint32_t flags) {
  operands.clear();
  special_registers.clear();
  access_bits.Clear();

  this->name = name;
  this->info = info;
  this->flags = flags;

  if (flags & InstrDisasm::kOE) {
    InstrRegister i = {
      InstrRegister::kXER, 0, InstrRegister::kReadWrite
    };
    special_registers.push_back(i);
  }
  if (flags & InstrDisasm::kRc) {
    InstrRegister i = {
      InstrRegister::kCR, 0, InstrRegister::kWrite
    };
    special_registers.push_back(i);
  }
  if (flags & InstrDisasm::kCA) {
    InstrRegister i = {
      InstrRegister::kXER, 0, InstrRegister::kReadWrite
    };
    special_registers.push_back(i);
  }
  if (flags & InstrDisasm::kLR) {
    InstrRegister i = {
      InstrRegister::kLR, 0, InstrRegister::kWrite
    };
    special_registers.push_back(i);
  }
}

void InstrDisasm::AddLR(InstrRegister::Access access) {
  InstrRegister i = {
    InstrRegister::kLR, 0, access
  };
  special_registers.push_back(i);
}

void InstrDisasm::AddCTR(InstrRegister::Access access) {
  InstrRegister i = {
    InstrRegister::kCTR, 0, access
  };
  special_registers.push_back(i);
}

void InstrDisasm::AddCR(uint32_t bf, InstrRegister::Access access) {
  InstrRegister i = {
    InstrRegister::kCR, bf, access
  };
  special_registers.push_back(i);
}

void InstrDisasm::AddFPSCR(InstrRegister::Access access) {
  InstrRegister i = {
    InstrRegister::kFPSCR, 0, access
  };
  special_registers.push_back(i);
}

void InstrDisasm::AddRegOperand(
    InstrRegister::RegisterSet set, uint32_t ordinal,
    InstrRegister::Access access, const char* display) {
  InstrRegister i = {
    set, ordinal, access
  };
  InstrOperand o;
  o.type    = InstrOperand::kRegister;
  o.reg     = i;
  o.display = display;
  operands.push_back(o);
}

void InstrDisasm::AddSImmOperand(uint64_t value, size_t width,
                                 const char* display) {
  InstrOperand o;
  o.type = InstrOperand::kImmediate;
  o.imm.is_signed = true;
  o.imm.value     = value;
  o.imm.width     = width;
  o.display       = display;
  operands.push_back(o);
}

void InstrDisasm::AddUImmOperand(uint64_t value, size_t width,
                                 const char* display) {
  InstrOperand o;
  o.type = InstrOperand::kImmediate;
  o.imm.is_signed = false;
  o.imm.value     = value;
  o.imm.width     = width;
  o.display       = display;
  operands.push_back(o);
}

int InstrDisasm::Finish() {
  for (std::vector<InstrOperand>::iterator it = operands.begin();
       it != operands.end(); ++it) {
    if (it->type == InstrOperand::kRegister) {
      access_bits.MarkAccess(it->reg);
    }
  }
  for (std::vector<InstrRegister>::iterator it = special_registers.begin();
       it != special_registers.end(); ++it) {
    access_bits.MarkAccess(*it);
  }
  return 0;
}

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

  if (operands.size()) {
    if (pad && out_str.size() < pad) {
      out_str += std::string(pad - out_str.size(), ' ');
    }
    for (std::vector<InstrOperand>::iterator it = operands.begin();
         it != operands.end(); ++it) {
      it->Dump(out_str);
      if (it + 1 != operands.end()) {
        out_str += ", ";
      }
    }
  }
}


InstrType* alloy::frontend::ppc::GetInstrType(uint32_t code) {
  // Fast lookup via tables.
  InstrType* slot = NULL;
  switch (code >> 26) {
  case 4:
    // Opcode = 4, index = bits 10-0 (10)
    slot = alloy::frontend::ppc::tables::instr_table_4[XESELECTBITS(code, 0, 10)];
    break;
  case 19:
    // Opcode = 19, index = bits 10-1 (10)
    slot = alloy::frontend::ppc::tables::instr_table_19[XESELECTBITS(code, 1, 10)];
    break;
  case 30:
    // Opcode = 30, index = bits 4-1 (4)
    // Special cased to an uber instruction.
    slot = alloy::frontend::ppc::tables::instr_table_30[XESELECTBITS(code, 0, 0)];
    break;
  case 31:
    // Opcode = 31, index = bits 10-1 (10)
    slot = alloy::frontend::ppc::tables::instr_table_31[XESELECTBITS(code, 1, 10)];
    break;
  case 58:
    // Opcode = 58, index = bits 1-0 (2)
    slot = alloy::frontend::ppc::tables::instr_table_58[XESELECTBITS(code, 0, 1)];
    break;
  case 59:
    // Opcode = 59, index = bits 5-1 (5)
    slot = alloy::frontend::ppc::tables::instr_table_59[XESELECTBITS(code, 1, 5)];
    break;
  case 62:
    // Opcode = 62, index = bits 1-0 (2)
    slot = alloy::frontend::ppc::tables::instr_table_62[XESELECTBITS(code, 0, 1)];
    break;
  case 63:
    // Opcode = 63, index = bits 10-1 (10)
    slot = alloy::frontend::ppc::tables::instr_table_63[XESELECTBITS(code, 1, 10)];
    break;
  default:
    slot = alloy::frontend::ppc::tables::instr_table[XESELECTBITS(code, 26, 31)];
    break;
  }
  if (slot && slot->opcode) {
    return slot;
  }

  // Slow lookup via linear scan.
  // This is primarily due to laziness. It could be made fast like the others.
  for (size_t n = 0;
       n < XECOUNT(alloy::frontend::ppc::tables::instr_table_scan);
       n++) {
    slot = &(alloy::frontend::ppc::tables::instr_table_scan[n]);
    if (slot->opcode == (code & slot->opcode_mask)) {
      return slot;
    }
  }

  return NULL;
}

int alloy::frontend::ppc::RegisterInstrDisassemble(
    uint32_t code, InstrDisassembleFn disassemble) {
  InstrType* instr_type = GetInstrType(code);
  XEASSERTNOTNULL(instr_type);
  if (!instr_type) {
    return 1;
  }
  XEASSERTNULL(instr_type->disassemble);
  instr_type->disassemble = disassemble;
  return 0;
}

int alloy::frontend::ppc::RegisterInstrEmit(uint32_t code, InstrEmitFn emit) {
  InstrType* instr_type = GetInstrType(code);
  XEASSERTNOTNULL(instr_type);
  if (!instr_type) {
    return 1;
  }
  XEASSERTNULL(instr_type->emit);
  instr_type->emit = emit;
  return 0;
}
