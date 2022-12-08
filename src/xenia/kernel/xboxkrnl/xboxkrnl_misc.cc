/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/cpu/ppc/ppc_frontend.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"
namespace xe {
namespace kernel {
namespace xboxkrnl {

void KeEnableFpuExceptions_entry(
    const ppc_context_t& ctx) {  // dword_t enabled) {
  // TODO(benvanik): can we do anything about exceptions?
  // theres a lot more thats supposed to happen here, the floating point state has to be saved to kthread, the irql changes, the machine state register is changed to enable exceptions

  X_KTHREAD* kthread = ctx->TranslateVirtual<X_KTHREAD*>(
      ctx->TranslateVirtualGPR<X_KPCR*>(ctx->r[13])->current_thread);
  kthread->fpu_exceptions_on = static_cast<uint32_t>(ctx->r[3]) != 0;
}
DECLARE_XBOXKRNL_EXPORT1(KeEnableFpuExceptions, kNone, kStub);
#if 0
struct __declspec(align(8)) fpucontext_ptr_t {
  char unknown_data[158];
  __int16 field_9E;
  char field_A0[2272];
  unsigned __int64 saved_FPSCR;
  double saved_fpu_regs[32];
};
#pragma pack(push, 1)
struct __declspec(align(1)) r13_struct_t {
  char field_0[6];
  __int16 field_6;
  char field_8[2];
  char field_A;
  char field_B[5];
  int field_10;
  char field_14[315];
  char field_14F;
  unsigned int field_150;
  char field_154[427];
  char field_2FF;
  char field_300;
};
#pragma pack(pop)


static uint64_t Do_mfmsr(ppc_context_t& ctx) {
  auto frontend = ctx->thread_state->processor()->frontend();
  cpu::ppc::CheckGlobalLock(
      ctx, reinterpret_cast<void*>(&xe::global_critical_region::mutex()),
      reinterpret_cast<void*>(&frontend->builtins()->global_lock_count));
  return ctx->scratch;
}

void KeSaveFloatingPointState_entry(ppc_context_t& ctx) {
  xe::Memory* memory = ctx->thread_state->memory();
  unsigned int r13 = static_cast<unsigned int>(ctx->r[13]);



  
  r13_struct_t* st = memory->TranslateVirtual<r13_struct_t*>(r13);
  /*
                 lwz       r10, 0x150(r13)
                lbz       r11, 0xA(r13)
                tweqi     r10, 0
                twnei     r11, 0
  */

  unsigned int r10 = st->field_150;
  unsigned char r11 = st->field_A;

  if (r10 == 0 || r11 != 0) {
	  //trap!
  }

  //should do mfmsr here
  
  unsigned int r3 = xe::load_and_swap<unsigned int>(&st->field_10);
  
  //too much work to do the mfmsr/mtmsr stuff right now
  int to_store = -2049;
  xe::store_and_swap(&st->field_10, (unsigned int)to_store);
  xe::store_and_swap(&st->field_6, (short)to_store);
 


  if (r3 != ~0u) {
    fpucontext_ptr_t* fpucontext =
        memory->TranslateVirtual<fpucontext_ptr_t*>(r3);
    xe::store_and_swap<uint64_t>(&fpucontext->saved_FPSCR, ctx->fpscr.value);
	
    for (unsigned int i = 0; i < 32; ++i) {
      xe::store_and_swap(&fpucontext->saved_fpu_regs[i], ctx->f[i]);
	}
    xe::store_and_swap<unsigned short>(&fpucontext->field_9E, 0xD7FF);
  }
  ctx->processor->backend()->SetGuestRoundingMode(ctx.value(), 0);
  ctx->fpscr.value = 0;
  st->field_A = 1;

  xe::store_and_swap(&st->field_10, r13 + 0x300);
  ctx->r[3] = r3;

}

DECLARE_XBOXKRNL_EXPORT1(KeSaveFloatingPointState, kNone, kImplemented);
#endif

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Misc);
