/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_INSTR_CTX_H_
#define XENIA_CPU_PPC_INSTR_CTX_H_

#include <xenia/cpu/ppc/instr.h>


namespace xe {
namespace cpu {
namespace ppc {


class InstrContext {
  using namespace llvm;
public:
  InstrContext();

  Value* cia();
  Value* nia();
  void set_nia(Value* value);
  Value* spr(uint32_t n);
  void set_spr(uint32_t n, Value* value);

  Value* cr();
  void set_cr(Value* value);

  Value* gpr(uint32_t n);
  void set_gpr(uint32_t n, Value* value);

  Value* get_memory_addr(uint32_t addr);
  Value* read_memory(Value* addr, uint32_t size, bool extend);
  void write_memory(Value* addr, uint32_t size, Value* value);

  LLVMContext&  context;
  Module&       module;
  // TODO(benvanik): IRBuilder/etc

  // Address of the instruction being generated.
  uint32_t      cia;

private:
  //
};


}  // namespace ppc
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_PPC_INSTR_CTX_H_
