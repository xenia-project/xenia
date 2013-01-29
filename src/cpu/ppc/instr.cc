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


void InstrDisasm::Init(std::string name, std::string info, uint32_t flags) {
  operands.clear();
  special_registers.clear();

  if (flags & InstrDisasm::kOE) {
    name += "o";
    special_registers.push_back((InstrRegister){
      InstrRegister::kXER, 0, InstrRegister::kReadWrite
    });
  }
  if (flags & InstrDisasm::kRc) {
    name += ".";
    special_registers.push_back((InstrRegister){
      InstrRegister::kCR, 0, InstrRegister::kWrite
    });
  }
  if (flags & InstrDisasm::kCA) {
    special_registers.push_back((InstrRegister){
      InstrRegister::kXER, 0, InstrRegister::kReadWrite
    });
  }
  if (flags & InstrDisasm::kLR) {
    name += "l";
    special_registers.push_back((InstrRegister){
      InstrRegister::kLR, 0, InstrRegister::kWrite
    });
  }

  XEIGNORE(xestrcpya(this->name, XECOUNT(this->name), name.c_str()));

  XEIGNORE(xestrcpya(this->info, XECOUNT(this->info), info.c_str()));
}

void InstrDisasm::AddLR(InstrRegister::Access access) {
  special_registers.push_back((InstrRegister){
    InstrRegister::kLR, 0, access
  });
}

void InstrDisasm::AddCTR(InstrRegister::Access access) {
  special_registers.push_back((InstrRegister){
    InstrRegister::kCTR, 0, access
  });
}

void InstrDisasm::AddCR(uint32_t bf, InstrRegister::Access access) {
  special_registers.push_back((InstrRegister){
    InstrRegister::kCR, bf, access
  });
}

void InstrDisasm::AddRegOperand(
    InstrRegister::RegisterSet set, uint32_t ordinal,
    InstrRegister::Access access, std::string display) {
  InstrOperand o;
  o.type = InstrOperand::kRegister;
  o.reg = (InstrRegister){
    set, ordinal, access
  };
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
  // TODO(benvanik): setup fast checks
  reg_mask = 0;
  gpr_mask = 0;
  fpr_mask = 0;

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
