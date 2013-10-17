/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/x64/x64_jit.h>

#include <xenia/cpu/cpu-private.h>
#include <xenia/cpu/exec_module.h>
#include <xenia/cpu/sdb.h>

#include <asmjit/asmjit.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::sdb;
using namespace xe::cpu::x64;

using namespace AsmJit;


X64JIT::X64JIT(xe_memory_ref memory, SymbolTable* sym_table) :
    JIT(memory, sym_table),
    emitter_(NULL) {
  jit_lock_ = xe_mutex_alloc(10000);
}

X64JIT::~X64JIT() {
  delete emitter_;
  xe_mutex_free(jit_lock_);
}

int X64JIT::Setup() {
  int result_code = 1;

  // Check processor for support.
  result_code = CheckProcessor();
  if (result_code) {
    XELOGE("Processor check failed, aborting...");
  }
  XEEXPECTZERO(result_code);

  // Create the emitter used to generate functions.
  emitter_ = new X64Emitter(memory_);

  result_code = 0;
XECLEANUP:
  return result_code;
}

void X64JIT::SetupGpuPointers(void* gpu_this,
                              void* gpu_read, void* gpu_write) {
  emitter_->SetupGpuPointers(gpu_this, gpu_read, gpu_write);
}

namespace {
struct BitDescription {
  uint32_t mask;
  const char* description;
};
static const BitDescription x86Features[] = {
  { kX86FeatureRdtsc              , "RDTSC" },
  { kX86FeatureRdtscP             , "RDTSCP" },
  { kX86FeatureCMov               , "CMOV" },
  { kX86FeatureCmpXchg8B          , "CMPXCHG8B" },
  { kX86FeatureCmpXchg16B         , "CMPXCHG16B" },
  { kX86FeatureClFlush            , "CLFLUSH" },
  { kX86FeaturePrefetch           , "PREFETCH" },
  { kX86FeatureLahfSahf           , "LAHF/SAHF" },
  { kX86FeatureFXSR               , "FXSAVE/FXRSTOR" },
  { kX86FeatureFFXSR              , "FXSAVE/FXRSTOR Optimizations" },
  { kX86FeatureMmx                , "MMX" },
  { kX86FeatureMmxExt             , "MMX Extensions" },
  { kX86Feature3dNow              , "3dNow!" },
  { kX86Feature3dNowExt           , "3dNow! Extensions" },
  { kX86FeatureSse                , "SSE" },
  { kX86FeatureSse2               , "SSE2" },
  { kX86FeatureSse3               , "SSE3" },
  { kX86FeatureSsse3              , "SSSE3" },
  { kX86FeatureSse4A              , "SSE4A" },
  { kX86FeatureSse41              , "SSE4.1" },
  { kX86FeatureSse42              , "SSE4.2" },
  { kX86FeatureAvx                , "AVX" },
  { kX86FeatureMSse               , "Misaligned SSE" },
  { kX86FeatureMonitorMWait       , "MONITOR/MWAIT" },
  { kX86FeatureMovBE              , "MOVBE" },
  { kX86FeaturePopCnt             , "POPCNT" },
  { kX86FeatureLzCnt              , "LZCNT" },
  { kX86FeaturePclMulDQ           , "PCLMULDQ" },
  { kX86FeatureMultiThreading     , "Multi-Threading" },
  { kX86FeatureExecuteDisableBit  , "Execute-Disable Bit" },
  { kX86Feature64Bit              , "64-Bit Processor" },
  { 0, NULL },
};
}

int X64JIT::CheckProcessor() {
  const CpuInfo* cpu = CpuInfo::getGlobal();
  const X86CpuInfo* x86Cpu = static_cast<const X86CpuInfo*>(cpu);
  const uint32_t mask = cpu->getFeatures();

  // TODO(benvanik): ensure features we want are supported.

  // TODO(benvanik): check for SSE modes we use.
  if (!(mask & kX86FeatureSse3)) {
    XELOGE("CPU does not support SSE3+ instructions!");
    DumpCPUInfo();
    return 1;
  }
  if (!(mask & kX86FeatureSse41)) {
    XELOGW("CPU does not support SSE4.1+ instructions, performance degraded!");
    DumpCPUInfo();
  }

  return 0;
}

void X64JIT::DumpCPUInfo() {
  const CpuInfo* cpu = CpuInfo::getGlobal();
  const X86CpuInfo* x86Cpu = static_cast<const X86CpuInfo*>(cpu);
  const uint32_t mask = cpu->getFeatures();

  XELOGCPU("Processor Info:");
  XELOGCPU("  Vendor string         : %s", cpu->getVendorString());
  XELOGCPU("  Brand string          : %s", cpu->getBrandString());
  XELOGCPU("  Family                : %u", cpu->getFamily());
  XELOGCPU("  Model                 : %u", cpu->getModel());
  XELOGCPU("  Stepping              : %u", cpu->getStepping());
  XELOGCPU("  Number of Processors  : %u", cpu->getNumberOfProcessors());
  XELOGCPU("  Features              : 0x%08X", cpu->getFeatures());
  XELOGCPU("  Bugs                  : 0x%08X", cpu->getBugs());
  XELOGCPU("  Processor Type        : %u", x86Cpu->getProcessorType());
  XELOGCPU("  Brand Index           : %u", x86Cpu->getBrandIndex());
  XELOGCPU("  CL Flush Cache Line   : %u", x86Cpu->getFlushCacheLineSize());
  XELOGCPU("  Max logical Processors: %u", x86Cpu->getMaxLogicalProcessors());
  XELOGCPU("  APIC Physical ID      : %u", x86Cpu->getApicPhysicalId());
  XELOGCPU("  Features:");
  for (const BitDescription* d = x86Features; d->mask; d++) {
    if (mask & d->mask) {
      XELOGCPU("    %s", d->description);
    }
  }
}

int X64JIT::InitModule(ExecModule* module) {
  // TODO(benvanik): precompile interesting functions (kernel calls, etc).
  // TODO(benvanik): warn on unimplemented instructions.
  // TODO(benvanik): dump instruction use report.
  // TODO(benvanik): dump kernel use report.
  // TODO(benvanik): check for cached code/patches/etc.
  return 0;
}

int X64JIT::UninitModule(ExecModule* module) {
  return 0;
}

void* X64JIT::GetFunctionPointer(sdb::FunctionSymbol* fn_symbol) {
  // Check function.
  // TODO(benvanik): make this lock-free (or spin).
  xe_mutex_lock(jit_lock_);
  x64_function_t fn_ptr = (x64_function_t)fn_symbol->impl_value;
  if (!fn_ptr) {
    // Function hasn't been prepped yet - make it now inline.
    // The emitter will lock and do other fancy things, if required.
    if (emitter_->PrepareFunction(fn_symbol)) {
      xe_mutex_unlock(jit_lock_);
      return NULL;
    }
    fn_ptr = (x64_function_t)fn_symbol->impl_value;
    XEASSERTNOTNULL(fn_ptr);
  }
  xe_mutex_unlock(jit_lock_);
  return fn_ptr;
}

int X64JIT::Execute(xe_ppc_state_t* ppc_state, FunctionSymbol* fn_symbol) {
  XELOGCPU("Execute(%.8X): %s...", fn_symbol->start_address, fn_symbol->name());

  x64_function_t fn_ptr = (x64_function_t)GetFunctionPointer(fn_symbol);
  if (!fn_ptr) {
    XELOGCPU("Execute(%.8X): unable to make function %s",
        fn_symbol->start_address, fn_symbol->name());
    return 1;
  }

  // Call into the function. This will compile it if needed.
  fn_ptr(ppc_state, ppc_state->lr);

  return 0;
}
