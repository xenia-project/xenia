/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/global_exports.h>

#include <xenia/cpu/sdb.h>
#include <xenia/cpu/ppc/instr.h>
#include <xenia/cpu/ppc/state.h>
#include <xenia/kernel/export.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::sdb;
using namespace xe::kernel;


namespace {


void _cdecl XeTrap(
    xe_ppc_state_t* state, uint64_t cia) {
  XELOGE("TRAP");
  XEASSERTALWAYS();
}

void _cdecl XeIndirectBranch(
    xe_ppc_state_t* state, uint64_t target, uint64_t br_ia) {
  XELOGCPU("INDIRECT BRANCH %.8X -> %.8X",
           (uint32_t)br_ia, (uint32_t)target);
  XEASSERTALWAYS();
}

void _cdecl XeInvalidInstruction(
    xe_ppc_state_t* state, uint64_t cia, uint64_t data) {
  ppc::InstrData i;
  i.address = (uint32_t)cia;
  i.code = (uint32_t)data;
  i.type = ppc::GetInstrType(i.code);

  if (!i.type) {
    XELOGCPU("INVALID INSTRUCTION %.8X: %.8X ???",
             i.address, i.code);
  } else if (i.type->disassemble) {
    ppc::InstrDisasm d;
    i.type->disassemble(i, d);
    std::string disasm;
    d.Dump(disasm);
    XELOGCPU("INVALID INSTRUCTION %.8X: %.8X %s",
             i.address, i.code, disasm.c_str());
  } else {
    XELOGCPU("INVALID INSTRUCTION %.8X: %.8X %s",
             i.address, i.code, i.type->name);
  }
}

void _cdecl XeAccessViolation(
    xe_ppc_state_t* state, uint64_t cia, uint64_t ea) {
  XELOGE("INVALID ACCESS %.8X: tried to touch %.8X",
         cia, (uint32_t)ea);
  XEASSERTALWAYS();
}

void _cdecl XeTraceKernelCall(
    xe_ppc_state_t* state, uint64_t cia, uint64_t call_ia,
    KernelExport* kernel_export) {
  XELOGCPU("TRACE: %.8X -> k.%.8X (%s)",
           (uint32_t)call_ia - 4, (uint32_t)cia,
           kernel_export ? kernel_export->name : "unknown");
}

void _cdecl XeTraceUserCall(
    xe_ppc_state_t* state, uint64_t cia, uint64_t call_ia,
    FunctionSymbol* fn) {
  XELOGCPU("TRACE: %.8X -> u.%.8X (%s)",
           (uint32_t)call_ia - 4, (uint32_t)cia, fn->name());
}

void _cdecl XeTraceBranch(
    xe_ppc_state_t* state, uint64_t cia, uint64_t target_ia) {
  switch (target_ia) {
  case kXEPPCRegLR:
    target_ia = state->lr;
    break;
  case kXEPPCRegCTR:
    target_ia = state->ctr;
    break;
  }
  XELOGCPU("TRACE: %.8X -> b.%.8X",
           (uint32_t)cia, (uint32_t)target_ia);
}

void _cdecl XeTraceInstruction(
    xe_ppc_state_t* state, uint64_t cia, uint64_t data) {
  XELOGCPU(
      "\n"
      " lr=%.16llX ctr=%.16llX xer=%.16llX\n"
      " r0=%.16llX  r1=%.16llX  r2=%.16llX  r3=%.16llX\n"
      " r4=%.16llX  r5=%.16llX  r6=%.16llX  r7=%.16llX\n"
      " r8=%.16llX  r9=%.16llX r10=%.16llX r11=%.16llX\n"
      "r12=%.16llX r13=%.16llX r14=%.16llX r15=%.16llX\n"
      "r16=%.16llX r17=%.16llX r18=%.16llX r19=%.16llX\n"
      "r20=%.16llX r21=%.16llX r22=%.16llX r23=%.16llX\n"
      "r24=%.16llX r25=%.16llX r26=%.16llX r27=%.16llX\n"
      "r28=%.16llX r29=%.16llX r30=%.16llX r31=%.16llX\n",
      state->lr, state->ctr, state->xer,
      state->r[0], state->r[1], state->r[2], state->r[3],
      state->r[4], state->r[5], state->r[6], state->r[7],
      state->r[8], state->r[9], state->r[10], state->r[11],
      state->r[12], state->r[13], state->r[14], state->r[15],
      state->r[16], state->r[17], state->r[18], state->r[19],
      state->r[20], state->r[21], state->r[22], state->r[23],
      state->r[24], state->r[25], state->r[26], state->r[27],
      state->r[28], state->r[29], state->r[30], state->r[31]);

  ppc::InstrData i;
  i.address = (uint32_t)cia;
  i.code = (uint32_t)data;
  i.type = ppc::GetInstrType(i.code);
  if (i.type && i.type->disassemble) {
    ppc::InstrDisasm d;
    i.type->disassemble(i, d);
    std::string disasm;
    d.Dump(disasm);
    XELOGCPU("TRACE: %.8X %.8X %s %s",
           i.address, i.code,
           i.type && i.type->emit ? " " : "X",
           disasm.c_str());
  } else {
    XELOGCPU("TRACE: %.8X %.8X %s %s",
           i.address, i.code,
           i.type && i.type->emit ? " " : "X",
           i.type ? i.type->name : "<unknown>");
  }

  // if (cia == 0x82012074) {
  //   printf("BREAKBREAKBREAK\n");
  // }

  // TODO(benvanik): better disassembly, printing of current register values/etc
}


}


void xe::cpu::GetGlobalExports(GlobalExports* global_exports) {
  global_exports->XeTrap                = XeTrap;
  global_exports->XeIndirectBranch      = XeIndirectBranch;
  global_exports->XeInvalidInstruction  = XeInvalidInstruction;
  global_exports->XeAccessViolation     = XeAccessViolation;
  global_exports->XeTraceKernelCall     = XeTraceKernelCall;
  global_exports->XeTraceUserCall       = XeTraceUserCall;
  global_exports->XeTraceBranch         = XeTraceBranch;
  global_exports->XeTraceInstruction    = XeTraceInstruction;
}
