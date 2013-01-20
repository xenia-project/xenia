/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_STATE_H_
#define XENIA_CPU_PPC_STATE_H_

#include <xenia/common.h>


namespace xe {
namespace cpu {
namespace ppc {


namespace SPR {
  enum SPR_e {
    XER                   = 1,
    LR                    = 8,
    CTR                   = 9,
  };
}  // SPR


namespace FPRF {
  enum FPRF_e {
    QUIET_NAN             = 0x00088000,
    NEG_INFINITY          = 0x00090000,
    NEG_NORMALIZED        = 0x00010000,
    NEG_DENORMALIZED      = 0x00018000,
    NEG_ZERO              = 0x00048000,
    POS_ZERO              = 0x00040000,
    POS_DENORMALIZED      = 0x00028000,
    POS_NORMALIZED        = 0x00020000,
    POS_INFINITY          = 0x000A0000,
  };
}  // FPRF


typedef struct XECACHEALIGN64 {
  uint64_t    r[32];              // General purpose registers
  xefloat4_t  v[128];             // VMX128 vector registers
  double      f[32];              // Floating-point registers

  uint32_t    pc;                 // Current PC (CIA)
  uint32_t    npc;                // Next PC (NIA)
  uint64_t    xer;                // XER register
  uint64_t    lr;                 // Link register
  uint64_t    ctr;                // Count register

  union {
    uint32_t  value;
    struct {
      uint8_t lt          :1;     // Negative (LT) - result is negative
      uint8_t gt          :1;     // Positive (GT) - result is positive (and not zero)
      uint8_t eq          :1;     // Zero (EQ) - result is zero or a stwcx/stdcx completed successfully
      uint8_t so          :1;     // Summary Overflow (SO) - copy of XER[SO]
    } cr0;
    struct {
      uint8_t fx          :1;     // FP exception summary - copy of FPSCR[FX]
      uint8_t fex         :1;     // FP enabled exception summary - copy of FPSCR[FEX]
      uint8_t vx          :1;     // FP invalid operation exception summary - copy of FPSCR[VX]
      uint8_t ox          :1;     // FP overflow exception - copy of FPSCR[OX]
    } cr1;
  } cr;                           // Condition register

  union {
    uint32_t  value;
    struct {
      uint8_t fx          :1;     // FP exception summary                             -- sticky
      uint8_t fex         :1;     // FP enabled exception summary
      uint8_t vx          :1;     // FP invalid operation exception summary
      uint8_t ox          :1;     // FP overflow exception                            -- sticky
      uint8_t ux          :1;     // FP underflow exception                           -- sticky
      uint8_t zx          :1;     // FP zero divide exception                         -- sticky
      uint8_t xx          :1;     // FP inexact exception                             -- sticky
      uint8_t vxsnan      :1;     // FP invalid op exception: SNaN                    -- sticky
      uint8_t vxisi       :1;     // FP invalid op exception: infinity - infinity     -- sticky
      uint8_t vxidi       :1;     // FP invalid op exception: infinity / infinity     -- sticky
      uint8_t vxzdz       :1;     // FP invalid op exception: 0 / 0                   -- sticky
      uint8_t vximz       :1;     // FP invalid op exception: infinity * 0            -- sticky
      uint8_t vxvc        :1;     // FP invalid op exception: invalid compare         -- sticky
      uint8_t fr          :1;     // FP fraction rounded
      uint8_t fi          :1;     // FP fraction inexact
      uint8_t fprf_c      :1;     // FP result class
      uint8_t fprf_lt     :1;     // FP result less than or negative (FL or <)
      uint8_t fprf_gt     :1;     // FP result greater than or positive (FG or >)
      uint8_t fprf_eq     :1;     // FP result equal or zero (FE or =)
      uint8_t fprf_un     :1;     // FP result unordered or NaN (FU or ?)
      uint8_t reserved    :1;
      uint8_t vxsoft      :1;     // FP invalid op exception: software request        -- sticky
      uint8_t vxsqrt      :1;     // FP invalid op exception: invalid sqrt            -- sticky
      uint8_t vxcvi       :1;     // FP invalid op exception: invalid integer convert -- sticky
      uint8_t ve          :1;     // FP invalid op exception enable
      uint8_t oe          :1;     // IEEE floating-point overflow exception enable
      uint8_t ue          :1;     // IEEE floating-point underflow exception enable
      uint8_t ze          :1;     // IEEE floating-point zero divide exception enable
      uint8_t xe          :1;     // IEEE floating-point inexact exception enable
      uint8_t ni          :1;     // Floating-point non-IEEE mode
      uint8_t rn          :2;     // FP rounding control: 00 = nearest
                                  //                      01 = toward zero
                                  //                      10 = toward +infinity
                                  //                      11 = toward -infinity
    } bits;
  } fpscr;                        // Floating-point status and control register

  uint32_t get_fprf() {
    return fpscr.value & 0x000F8000;
  }
  void set_fprf(const uint32_t v) {
    fpscr.value = (fpscr.value & ~0x000F8000) | v;
  }
} PpcRegisters;


typedef struct {
  PpcRegisters      registers;
} PpcState;


}  // namespace ppc
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_PPC_STATE_H_
