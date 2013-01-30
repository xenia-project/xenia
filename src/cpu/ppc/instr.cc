/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/ppc/instr.h>

#include <sstream>

#include "cpu/ppc/instr_tables.h"


using namespace xe::cpu::ppc;


void InstrAccessBits::Clear() {
  spr = cr = gpr = fpr = 0;
}

void InstrAccessBits::Extend(InstrAccessBits& other) {
    spr |= other.spr;
    cr  |= other.cr;
    gpr |= other.gpr;
    fpr |= other.fpr;
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
    default:
    case InstrRegister::kVMX:
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

  out_str = str.str();
}


void InstrDisasm::Init(std::string name, std::string info, uint32_t flags) {
  operands.clear();
  special_registers.clear();
  access_bits.Clear();

  if (flags & InstrDisasm::kOE) {
    name += "o";
    InstrRegister i = {
      InstrRegister::kXER, 0, InstrRegister::kReadWrite
    };
    special_registers.push_back(i);
  }
  if (flags & InstrDisasm::kRc) {
    name += ".";
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
    name += "l";
    InstrRegister i = {
      InstrRegister::kLR, 0, InstrRegister::kWrite
    };
    special_registers.push_back(i);
  }

  XEIGNORE(xestrcpya(this->name, XECOUNT(this->name), name.c_str()));

  XEIGNORE(xestrcpya(this->info, XECOUNT(this->info), info.c_str()));
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

void InstrDisasm::AddRegOperand(
    InstrRegister::RegisterSet set, uint32_t ordinal,
    InstrRegister::Access access, std::string display) {
  InstrRegister i = {
    set, ordinal, access
  };
  InstrOperand o;
  o.type = InstrOperand::kRegister;
  o.reg = i;
  if (!display.size()) {
    std::stringstream display_out;
    switch (set) {
      case InstrRegister::kXER:
        display_out << "XER";
        break;
      case InstrRegister::kLR:
        display_out << "LR";
        break;
      case InstrRegister::kCTR:
        display_out << "CTR";
        break;
      case InstrRegister::kCR:
        display_out << "CR";
        display_out << ordinal;
        break;
      case InstrRegister::kFPSCR:
        display_out << "FPSCR";
        break;
      case InstrRegister::kGPR:
        display_out << "r";
        display_out << ordinal;
        break;
      case InstrRegister::kFPR:
        display_out << "f";
        display_out << ordinal;
        break;
      case InstrRegister::kVMX:
        display_out << "v";
        display_out << ordinal;
        break;
    }
    display = display_out.str();
  }
  XEIGNORE(xestrcpya(o.display, XECOUNT(o.display), display.c_str()));
  operands.push_back(o);
}

void InstrDisasm::AddSImmOperand(uint64_t value, size_t width,
                                 std::string display) {
  InstrOperand o;
  o.type = InstrOperand::kImmediate;
  o.imm.is_signed = true;
  o.imm.value     = value;
  o.imm.width     = value;
  if (display.size()) {
    XEIGNORE(xestrcpya(o.display, XECOUNT(o.display), display.c_str()));
  } else {
    const size_t max_count = XECOUNT(o.display);
    switch (width) {
      case 1:
        xesnprintfa(o.display, max_count, "%d", (int32_t)(int8_t)value);
        break;
      case 2:
        xesnprintfa(o.display, max_count, "%d", (int32_t)(int16_t)value);
        break;
      case 4:
        xesnprintfa(o.display, max_count, "%d", (int32_t)value);
        break;
      case 8:
        xesnprintfa(o.display, max_count, "%lld", (int64_t)value);
        break;
    }
  }
  operands.push_back(o);
}

void InstrDisasm::AddUImmOperand(uint64_t value, size_t width,
                                 std::string display) {
  InstrOperand o;
  o.type = InstrOperand::kImmediate;
  o.imm.is_signed = false;
  o.imm.value     = value;
  o.imm.width     = value;
  if (display.size()) {
    XEIGNORE(xestrcpya(o.display, XECOUNT(o.display), display.c_str()));
  } else {
    const size_t max_count = XECOUNT(o.display);
    switch (width) {
      case 1:
        xesnprintfa(o.display, max_count, "0x%.2X", (uint8_t)value);
        break;
      case 2:
        xesnprintfa(o.display, max_count, "0x%.4X", (uint16_t)value);
        break;
      case 4:
        xesnprintfa(o.display, max_count, "0x%.8X", (uint32_t)value);
        break;
      case 8:
        xesnprintfa(o.display, max_count, "0x%.16llX", value);
        break;
    }
  }
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

void InstrDisasm::Dump(std::string& str, size_t pad) {
  str = name;
  if (operands.size()) {
    if (pad && str.size() < pad) {
      str += std::string(pad - str.size(), ' ');
    }
    for (std::vector<InstrOperand>::iterator it = operands.begin();
         it != operands.end(); ++it) {
      str += it->display;

      if (it + 1 != operands.end()) {
        str += ", ";
      }
    }
  }
}

InstrType* xe::cpu::ppc::GetInstrType(uint32_t code) {
  InstrType* slot = NULL;
  switch (code >> 26) {
  case 4:
    // Opcode = 4, index = bits 5-0 (6)
    slot = &xe::cpu::ppc::tables::instr_table_4[XESELECTBITS(code, 0, 5)];
    break;
  case 19:
    // Opcode = 19, index = bits 10-1 (10)
    slot = &xe::cpu::ppc::tables::instr_table_19[XESELECTBITS(code, 1, 10)];
    break;
  case 30:
    // Opcode = 30, index = bits 4-1 (4)
    slot = &xe::cpu::ppc::tables::instr_table_30[XESELECTBITS(code, 1, 4)];
    break;
  case 31:
    // Opcode = 31, index = bits 10-1 (10)
    slot = &xe::cpu::ppc::tables::instr_table_31[XESELECTBITS(code, 1, 10)];
    break;
  case 58:
    // Opcode = 58, index = bits 1-0 (2)
    slot = &xe::cpu::ppc::tables::instr_table_58[XESELECTBITS(code, 0, 1)];
    break;
  case 59:
    // Opcode = 59, index = bits 5-1 (5)
    slot = &xe::cpu::ppc::tables::instr_table_59[XESELECTBITS(code, 1, 5)];
    break;
  case 62:
    // Opcode = 62, index = bits 1-0 (2)
    slot = &xe::cpu::ppc::tables::instr_table_62[XESELECTBITS(code, 0, 1)];
    break;
  case 63:
    // Opcode = 63, index = bits 10-1 (10)
    slot = &xe::cpu::ppc::tables::instr_table_63[XESELECTBITS(code, 1, 10)];
    break;
  default:
    slot = &xe::cpu::ppc::tables::instr_table[XESELECTBITS(code, 26, 31)];
    break;
  }
  if (!slot || !slot->opcode) {
    return NULL;
  }
  return slot;
}

int xe::cpu::ppc::RegisterInstrDisassemble(
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

int xe::cpu::ppc::RegisterInstrEmit(uint32_t code, InstrEmitFn emit) {
  InstrType* instr_type = GetInstrType(code);
  XEASSERTNOTNULL(instr_type);
  if (!instr_type) {
    return 1;
  }
  XEASSERTNULL(instr_type->emit);
  instr_type->emit = emit;
  return 0;
}
