/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/x64/x64_emit.h>

#include <xenia/cpu/cpu-private.h>


using namespace xe::cpu;
using namespace xe::cpu::ppc;

using namespace AsmJit;


namespace xe {
namespace cpu {
namespace x64 {


// XEEMITTER(fnabsx,       0xFC000110, X  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
//   XEINSTRNOTIMPLEMENTED();
//   return 1;
// }


void X64RegisterEmitCategoryAltivec() {
  // XEREGISTERINSTR(faddx,        0xFC00002A);
}


}  // namespace x64
}  // namespace cpu
}  // namespace xe
