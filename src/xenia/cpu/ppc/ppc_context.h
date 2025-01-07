/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_PPC_CONTEXT_H_
#define XENIA_CPU_PPC_PPC_CONTEXT_H_

#include <cstdint>
#include <mutex>
#include <string>

#include "xenia/base/mutex.h"
#include "xenia/base/vec128.h"
#include "xenia/guest_pointers.h"
namespace xe {
namespace cpu {
class Processor;
class ThreadState;
}  // namespace cpu
namespace kernel {
class KernelState;
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace cpu {
namespace ppc {

// Map:
// 0-31: GPR
// 32-63: FPR
// 64: LR
// 65: CTR
// 66: XER
// 67: FPSCR
// 68: VSCR
// 69-76: CR0-7
// 100: invalid
// 128-256: VR

enum class PPCRegister {
  kR0 = 0,
  kR1,
  kR2,
  kR3,
  kR4,
  kR5,
  kR6,
  kR7,
  kR8,
  kR9,
  kR10,
  kR11,
  kR12,
  kR13,
  kR14,
  kR15,
  kR16,
  kR17,
  kR18,
  kR19,
  kR20,
  kR21,
  kR22,
  kR23,
  kR24,
  kR25,
  kR26,
  kR27,
  kR28,
  kR29,
  kR30,
  kR31,
  kFR0 = 32,
  kFR1,
  kFR2,
  kFR3,
  kFR4,
  kFR5,
  kFR6,
  kFR7,
  kFR8,
  kFR9,
  kFR10,
  kFR11,
  kFR12,
  kFR13,
  kFR14,
  kFR15,
  kFR16,
  kFR17,
  kFR18,
  kFR19,
  kFR20,
  kFR21,
  kFR22,
  kFR23,
  kFR24,
  kFR25,
  kFR26,
  kFR27,
  kFR28,
  kFR29,
  kFR30,
  kFR31,
  kVR0 = 64,
  kVR1,
  kVR2,
  kVR3,
  kVR4,
  kVR5,
  kVR6,
  kVR7,
  kVR8,
  kVR9,
  kVR10,
  kVR11,
  kVR12,
  kVR13,
  kVR14,
  kVR15,
  kVR16,
  kVR17,
  kVR18,
  kVR19,
  kVR20,
  kVR21,
  kVR22,
  kVR23,
  kVR24,
  kVR25,
  kVR26,
  kVR27,
  kVR28,
  kVR29,
  kVR30,
  kVR31,
  kVR32,
  kVR33,
  kVR34,
  kVR35,
  kVR36,
  kVR37,
  kVR38,
  kVR39,
  kVR40,
  kVR41,
  kVR42,
  kVR43,
  kVR44,
  kVR45,
  kVR46,
  kVR47,
  kVR48,
  kVR49,
  kVR50,
  kVR51,
  kVR52,
  kVR53,
  kVR54,
  kVR55,
  kVR56,
  kVR57,
  kVR58,
  kVR59,
  kVR60,
  kVR61,
  kVR62,
  kVR63,
  kVR64,
  kVR65,
  kVR66,
  kVR67,
  kVR68,
  kVR69,
  kVR70,
  kVR71,
  kVR72,
  kVR73,
  kVR74,
  kVR75,
  kVR76,
  kVR77,
  kVR78,
  kVR79,
  kVR80,
  kVR81,
  kVR82,
  kVR83,
  kVR84,
  kVR85,
  kVR86,
  kVR87,
  kVR88,
  kVR89,
  kVR90,
  kVR91,
  kVR92,
  kVR93,
  kVR94,
  kVR95,
  kVR96,
  kVR97,
  kVR98,
  kVR99,
  kVR100,
  kVR101,
  kVR102,
  kVR103,
  kVR104,
  kVR105,
  kVR106,
  kVR107,
  kVR108,
  kVR109,
  kVR110,
  kVR111,
  kVR112,
  kVR113,
  kVR114,
  kVR115,
  kVR116,
  kVR117,
  kVR118,
  kVR119,
  kVR120,
  kVR121,
  kVR122,
  kVR123,
  kVR124,
  kVR125,
  kVR126,
  kVR127,
  kVR128,

  kLR,
  kCTR,
  kXER,
  kFPSCR,
  kVSCR,
  kCR,
};

#pragma pack(push, 8)
typedef struct alignas(64) PPCContext_s {
  union {
    uint32_t value;
    struct {
      uint8_t cr0_lt;  // Negative (LT) - result is negative
      uint8_t cr0_gt;  // Positive (GT) - result is positive (and not zero)
      uint8_t cr0_eq;  // Zero (EQ) - result is zero or a stwcx/stdcx completed
                       // successfully
      uint8_t cr0_so;  // Summary Overflow (SO) - copy of XER[SO]
    };
  } cr0;  // 0xA24
  union {
    uint32_t value;
    struct {
      uint8_t cr1_fx;   // FP exception summary - copy of FPSCR[FX]
      uint8_t cr1_fex;  // FP enabled exception summary - copy of FPSCR[FEX]
      uint8_t
          cr1_vx;  // FP invalid operation exception summary - copy of FPSCR[VX]
      uint8_t cr1_ox;  // FP overflow exception - copy of FPSCR[OX]
    };
  } cr1;
  union {
    uint32_t value;
    struct {
      uint8_t cr2_0;
      uint8_t cr2_1;
      uint8_t cr2_2;
      uint8_t cr2_3;
    };
  } cr2;
  union {
    uint32_t value;
    struct {
      uint8_t cr3_0;
      uint8_t cr3_1;
      uint8_t cr3_2;
      uint8_t cr3_3;
    };
  } cr3;
  union {
    uint32_t value;
    struct {
      uint8_t cr4_0;
      uint8_t cr4_1;
      uint8_t cr4_2;
      uint8_t cr4_3;
    };
  } cr4;
  union {
    uint32_t value;
    struct {
      uint8_t cr5_0;
      uint8_t cr5_1;
      uint8_t cr5_2;
      uint8_t cr5_3;
    };
  } cr5;
  union {
    uint32_t value;
    struct {
      uint8_t cr6_all_equal;
      uint8_t cr6_1;
      uint8_t cr6_none_equal;
      uint8_t cr6_3;
    };
  } cr6;
  union {
    uint32_t value;
    struct {
      uint8_t cr7_0;
      uint8_t cr7_1;
      uint8_t cr7_2;
      uint8_t cr7_3;
    };
  } cr7;

  union {
    uint32_t value;
    struct {
      uint32_t rn : 2;      // FP rounding control: 00 = nearest
                            //                      01 = toward zero
                            //                      10 = toward +infinity
                            //                      11 = toward -infinity
      uint32_t ni : 1;      // Floating-point non-IEEE mode
      uint32_t xe : 1;      // IEEE floating-point inexact exception enable
      uint32_t ze : 1;      // IEEE floating-point zero divide exception enable
      uint32_t ue : 1;      // IEEE floating-point underflow exception enable
      uint32_t oe : 1;      // IEEE floating-point overflow exception enable
      uint32_t ve : 1;      // FP invalid op exception enable
      uint32_t vxcvi : 1;   // FP invalid op exception: invalid integer convert
                            // -- sticky
      uint32_t vxsqrt : 1;  // FP invalid op exception: invalid sqrt -- sticky
      uint32_t vxsoft : 1;  // FP invalid op exception: software request
                            // -- sticky
      uint32_t reserved : 1;
      uint32_t fprf_un : 1;  // FP result unordered or NaN (FU or ?)
      uint32_t fprf_eq : 1;  // FP result equal or zero (FE or =)
      uint32_t fprf_gt : 1;  // FP result greater than or positive (FG or >)
      uint32_t fprf_lt : 1;  // FP result less than or negative (FL or <)
      uint32_t fprf_c : 1;   // FP result class
      uint32_t fi : 1;       // FP fraction inexact
      uint32_t fr : 1;       // FP fraction rounded
      uint32_t vxvc : 1;  // FP invalid op exception: invalid compare         --
                          // sticky
      uint32_t vximz : 1;   // FP invalid op exception: infinity * 0 -- sticky
      uint32_t vxzdz : 1;   // FP invalid op exception: 0 / 0 -- sticky
      uint32_t vxidi : 1;   // FP invalid op exception: infinity / infinity
                            // -- sticky
      uint32_t vxisi : 1;   // FP invalid op exception: infinity - infinity
                            // -- sticky
      uint32_t vxsnan : 1;  // FP invalid op exception: SNaN -- sticky
      uint32_t
          xx : 1;  // FP inexact exception                             -- sticky
      uint32_t
          zx : 1;  // FP zero divide exception                         -- sticky
      uint32_t
          ux : 1;  // FP underflow exception                           -- sticky
      uint32_t
          ox : 1;  // FP overflow exception                            -- sticky
      uint32_t vx : 1;   // FP invalid operation exception summary
      uint32_t fex : 1;  // FP enabled exception summary
      uint32_t
          fx : 1;  // FP exception summary                             -- sticky
    } bits;
  } fpscr;  // Floating-point status and control register

  // Most frequently used registers first.

  uint64_t r[32];  // 0x20 General purpose registers
  uint64_t ctr;    // 0x18 Count register
  uint64_t lr;     // 0x10 Link register

  uint64_t msr;  // machine state register

  double f[32];     // 0x120 Floating-point registers
  vec128_t v[128];  // 0x220 VMX128 vector registers
  vec128_t vscr_vec;
  // XER register:
  // Split to make it easier to do individual updates.
  uint8_t xer_ca;
  uint8_t xer_ov;
  uint8_t xer_so;

  // Condition registers:
  // These are split to make it easier to do DCE on unused stores.
  uint64_t cr() const;
  void set_cr(uint64_t value);
  // todo: remove, saturation should be represented by a vector
  uint8_t vscr_sat;

  uint32_t vrsave;

  // uint32_t get_fprf() {
  //   return fpscr.value & 0x000F8000;
  // }
  // void set_fprf(const uint32_t v) {
  //   fpscr.value = (fpscr.value & ~0x000F8000) | v;
  // }

  // Thread ID assigned to this context.
  uint32_t thread_id;

  // Global interrupt lock, held while interrupts are disabled or interrupts are
  // executing. This is shared among all threads and comes from the processor.
  global_mutex_type* global_mutex;

  // Used to shuttle data into externs. Contents volatile.
  uint64_t scratch;

  // Processor-specific data pointer. Used on callbacks to get access to the
  // current runtime and its data.
  Processor* processor;

  // Shared kernel state, for easy access from kernel exports.
  xe::kernel::KernelState* kernel_state;

  uint8_t* physical_membase;

  // Value of last reserved load
  uint64_t reserved_val;
  ThreadState* thread_state;
  uint8_t* virtual_membase;

  template <typename T = uint8_t*>
  inline T TranslateVirtual(uint32_t guest_address) XE_RESTRICT const {
    static_assert(std::is_pointer_v<T>);
    uint8_t* host_address = virtual_membase + guest_address;
#if XE_PLATFORM_WIN32 == 1
    if (guest_address >=
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this))) {
      host_address += 0x1000;
    }
#endif
    return reinterpret_cast<T>(host_address);
  }
  template <typename T>
  inline xe::be<T>* TranslateVirtualBE(uint32_t guest_address)
      XE_RESTRICT const {
    static_assert(!std::is_pointer_v<T> &&
                  sizeof(T) > 1);  // maybe assert is_integral?
    return TranslateVirtual<xe::be<T>*>(guest_address);
  }
  // for convenience in kernel functions, version that auto narrows to uint32
  template <typename T = uint8_t*>
  inline T TranslateVirtualGPR(uint64_t guest_address) XE_RESTRICT const {
    return TranslateVirtual<T>(static_cast<uint32_t>(guest_address));
  }

  template <typename T>
  inline T* TranslateVirtual(TypedGuestPointer<T> guest_address) {
    return TranslateVirtual<T*>(guest_address.m_ptr);
  }
  template <typename T>
  inline uint32_t HostToGuestVirtual(T* host_ptr) XE_RESTRICT const {
    uint32_t guest_tmp = static_cast<uint32_t>(
        reinterpret_cast<const uint8_t*>(host_ptr) - virtual_membase);
#if XE_PLATFORM_WIN32 == 1
    if (guest_tmp >= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this))) {
      guest_tmp -= 0x1000;
    }
#endif
    return guest_tmp;
  }
  static std::string GetRegisterName(PPCRegister reg);
  std::string GetStringFromValue(PPCRegister reg) const;
  void SetValueFromString(PPCRegister reg, std::string value);

  void SetRegFromString(const char* name, const char* value);
  bool CompareRegWithString(const char* name, const char* value,
                            std::string& result) const;
} PPCContext;
#pragma pack(pop)
constexpr size_t ppcctx_size = sizeof(PPCContext);
static_assert(sizeof(PPCContext) % 64 == 0, "64b padded");

}  // namespace ppc
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PPC_PPC_CONTEXT_H_
