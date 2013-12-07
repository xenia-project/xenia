/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_FRONTEND_PPC_PPC_CONTEXT_H_
#define ALLOY_FRONTEND_PPC_PPC_CONTEXT_H_

#include <alloy/core.h>


namespace alloy { namespace runtime {
  class Runtime;
  class ThreadState;
} }


namespace alloy {
namespace frontend {
namespace ppc {


using vec128_t = alloy::vec128_t;


typedef union {
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
  struct {
    uint8_t value       :4;
  } cr2;
  struct {
    uint8_t value       :4;
  } cr3;
  struct {
    uint8_t value       :4;
  } cr4;
  struct {
    uint8_t value       :4;
  } cr5;
  struct {
    uint8_t value       :4;
  } cr6;
  struct {
    uint8_t value       :4;
  } cr7;
} PPCCR;


#pragma pack(push, 4)
typedef struct XECACHEALIGN64 PPCContext_s {
  // Most frequently used registers first.
  uint64_t    r[32];            // General purpose registers
  uint64_t    lr;                 // Link register
  uint64_t    ctr;                // Count register

  // XER register
  // Split to make it easier to do individual updates.
  uint8_t     xer_ca;
  uint8_t     xer_ov;
  uint8_t     xer_so;

  // Condition registers
  // These are split to make it easier to do DCE on unused stores.
  union {
    uint32_t  value;
    struct {
      uint8_t   cr0_lt;             // Negative (LT) - result is negative
      uint8_t   cr0_gt;             // Positive (GT) - result is positive (and not zero)
      uint8_t   cr0_eq;             // Zero (EQ) - result is zero or a stwcx/stdcx completed successfully
      uint8_t   cr0_so;             // Summary Overflow (SO) - copy of XER[SO]
    };
  } cr0;
  union {
    uint32_t  value;
    struct {
      uint8_t   cr1_fx;             // FP exception summary - copy of FPSCR[FX]
      uint8_t   cr1_fex;            // FP enabled exception summary - copy of FPSCR[FEX]
      uint8_t   cr1_vx;             // FP invalid operation exception summary - copy of FPSCR[VX]
      uint8_t   cr1_ox;             // FP overflow exception - copy of FPSCR[OX]
    };
  } cr1;
  union {
    uint32_t  value;
    struct {
      uint8_t   cr2_0;    uint8_t   cr2_1;    uint8_t   cr2_2;    uint8_t   cr2_3;
    };
  } cr2;
  union {
    uint32_t  value;
    struct {
      uint8_t   cr3_0;    uint8_t   cr3_1;    uint8_t   cr3_2;    uint8_t   cr3_3;
    };
  } cr3;
  union {
    uint32_t  value;
    struct {
      uint8_t   cr4_0;    uint8_t   cr4_1;    uint8_t   cr4_2;    uint8_t   cr4_3;
    };
  } cr4;
  union {
    uint32_t  value;
    struct {
      uint8_t   cr5_0;    uint8_t   cr5_1;    uint8_t   cr5_2;    uint8_t   cr5_3;
    };
  } cr5;
  union {
    uint32_t  value;
    struct {
      uint8_t   cr6_0;
      uint8_t   cr6_none_equal;
      uint8_t   cr6_2;
      uint8_t   cr6_all_equal;
    };
  } cr6;
  union {
    uint32_t  value;
    struct {
      uint8_t   cr7_0;    uint8_t   cr7_1;    uint8_t   cr7_2;    uint8_t   cr7_3;
    };
  } cr7;

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

  double        f[32];            // Floating-point registers
  vec128_t      v[128];           // VMX128 vector registers

  // uint32_t get_fprf() {
  //   return fpscr.value & 0x000F8000;
  // }
  // void set_fprf(const uint32_t v) {
  //   fpscr.value = (fpscr.value & ~0x000F8000) | v;
  // }

  // Runtime-specific data pointer. Used on callbacks to get access to the
  // current runtime and its data.
  uint8_t*              membase;
  runtime::Runtime*     runtime;
  runtime::ThreadState* thread_state;

  void SetRegFromString(const char* name, const char* value);
  bool CompareRegWithString(const char* name, const char* value,
                            char* out_value, size_t out_value_size);
} PPCContext;
#pragma pack(pop)


}  // namespace ppc
}  // namespace frontend
}  // namespace alloy


#endif  // ALLOY_FRONTEND_PPC_PPC_CONTEXT_H_
