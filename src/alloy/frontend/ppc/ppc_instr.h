/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_FRONTEND_PPC_PPC_INSTR_H_
#define ALLOY_FRONTEND_PPC_PPC_INSTR_H_

#include <alloy/core.h>

#include <string>
#include <vector>


namespace alloy {
namespace frontend {
namespace ppc {


// TODO(benvanik): rename these
typedef enum {
  kXEPPCInstrFormatI        = 0,
  kXEPPCInstrFormatB        = 1,
  kXEPPCInstrFormatSC       = 2,
  kXEPPCInstrFormatD        = 3,
  kXEPPCInstrFormatDS       = 4,
  kXEPPCInstrFormatX        = 5,
  kXEPPCInstrFormatXL       = 6,
  kXEPPCInstrFormatXFX      = 7,
  kXEPPCInstrFormatXFL      = 8,
  kXEPPCInstrFormatXS       = 9,
  kXEPPCInstrFormatXO       = 10,
  kXEPPCInstrFormatA        = 11,
  kXEPPCInstrFormatM        = 12,
  kXEPPCInstrFormatMD       = 13,
  kXEPPCInstrFormatMDS      = 14,
  kXEPPCInstrFormatVXA      = 15,
  kXEPPCInstrFormatVX       = 16,
  kXEPPCInstrFormatVXR      = 17,
  kXEPPCInstrFormatVX128    = 18,
  kXEPPCInstrFormatVX128_1  = 19,
  kXEPPCInstrFormatVX128_2  = 20,
  kXEPPCInstrFormatVX128_3  = 21,
  kXEPPCInstrFormatVX128_4  = 22,
  kXEPPCInstrFormatVX128_5  = 23,
  kXEPPCInstrFormatVX128_P  = 24,
  kXEPPCInstrFormatVX128_R  = 25,
  kXEPPCInstrFormatXDSS     = 26,
} xe_ppc_instr_format_e;

typedef enum {
  kXEPPCInstrMaskVXR        = 0xFC0003FF,
  kXEPPCInstrMaskVXA        = 0xFC00003F,
  kXEPPCInstrMaskVX128      = 0xFC0003D0,
  kXEPPCInstrMaskVX128_1    = 0xFC0007F3,
  kXEPPCInstrMaskVX128_2    = 0xFC000210,
  kXEPPCInstrMaskVX128_3    = 0xFC0007F0,
  kXEPPCInstrMaskVX128_4    = 0xFC000730,
  kXEPPCInstrMaskVX128_5    = 0xFC000010,
  kXEPPCInstrMaskVX128_P    = 0xFC000630,
  kXEPPCInstrMaskVX128_R    = 0xFC000390,
} xe_ppc_instr_mask_e;

typedef enum {
  kXEPPCInstrTypeGeneral      = (1 << 0),
  kXEPPCInstrTypeBranch       = (1 << 1),
  kXEPPCInstrTypeBranchCond   = kXEPPCInstrTypeBranch | (1 << 2),
  kXEPPCInstrTypeBranchAlways = kXEPPCInstrTypeBranch | (1 << 3),
  kXEPPCInstrTypeSyscall      = (1 << 4),
} xe_ppc_instr_type_e;

typedef enum {
  kXEPPCInstrFlagReserved     = 0,
} xe_ppc_instr_flag_e;


class InstrType;


static inline int64_t XEEXTS16(uint32_t v) {
  return (int64_t)((int16_t)v);
}
static inline int64_t XEEXTS26(uint32_t v) {
  return (int64_t)(v & 0x02000000 ? (int32_t)v | 0xFC000000 : (int32_t)(v));
}
static inline uint64_t XEEXTZ16(uint32_t v) {
  return (uint64_t)((uint16_t)v);
}
static inline uint64_t XEMASK(uint32_t mstart, uint32_t mstop) {
  // if mstart ≤ mstop then
  //   mask[mstart:mstop] = ones
  //   mask[all other bits] = zeros
  // else
  //   mask[mstart:63] = ones
  //   mask[0:mstop] = ones
  //   mask[all other bits] = zeros
  mstart &= 0x3F;
  mstop &= 0x3F;
  uint64_t value =
      (UINT64_MAX >> mstart) ^ ((mstop >= 63) ? 0 : UINT64_MAX >> (mstop + 1));
  return mstart <= mstop ? value : ~value;
}


typedef struct {
  InstrType*        type;
  uint64_t          address;

  union {
    uint32_t          code;

    // kXEPPCInstrFormatI
    struct {
      uint32_t        LK      : 1;
      uint32_t        AA      : 1;
      uint32_t        LI      : 24;
      uint32_t                : 6;
    } I;
    // kXEPPCInstrFormatB
    struct {
      uint32_t        LK      : 1;
      uint32_t        AA      : 1;
      uint32_t        BD      : 14;
      uint32_t        BI      : 5;
      uint32_t        BO      : 5;
      uint32_t                : 6;
    } B;

    // kXEPPCInstrFormatSC
    // kXEPPCInstrFormatD
    struct {
      uint32_t        DS      : 16;
      uint32_t        RA      : 5;
      uint32_t        RT      : 5;
      uint32_t                : 6;
    } D;
    // kXEPPCInstrFormatDS
    struct {
      uint32_t                : 2;
      uint32_t        DS      : 14;
      uint32_t        RA      : 5;
      uint32_t        RT      : 5;
      uint32_t                : 6;
    } DS;
    // kXEPPCInstrFormatX
    struct {
      uint32_t        Rc      : 1;
      uint32_t                : 10;
      uint32_t        RB      : 5;
      uint32_t        RA      : 5;
      uint32_t        RT      : 5;
      uint32_t                : 6;
    } X;
    // kXEPPCInstrFormatXL
    struct {
      uint32_t        LK      : 1;
      uint32_t                : 10;
      uint32_t        BB      : 5;
      uint32_t        BI      : 5;
      uint32_t        BO      : 5;
      uint32_t                : 6;
    } XL;
    // kXEPPCInstrFormatXFX
    struct {
      uint32_t                : 1;
      uint32_t                : 10;
      uint32_t        spr     : 10;
      uint32_t        RT      : 5;
      uint32_t                : 6;
    } XFX;
    // kXEPPCInstrFormatXFL
    struct {
      uint32_t        Rc      : 1;
      uint32_t                : 10;
      uint32_t        RB      : 5;
      uint32_t                : 1;
      uint32_t        FM      : 8;
      uint32_t                : 1;
      uint32_t                : 6;
    } XFL;
    // kXEPPCInstrFormatXS
    struct {
      uint32_t        Rc      : 1;
      uint32_t        SH5     : 1;
      uint32_t                : 9;
      uint32_t        SH      : 5;
      uint32_t        RA      : 5;
      uint32_t        RT      : 5;
      uint32_t                : 6;
    } XS;
    // kXEPPCInstrFormatXO
    struct {
      uint32_t        Rc      : 1;
      uint32_t                : 9;
      uint32_t        OE      : 1;
      uint32_t        RB      : 5;
      uint32_t        RA      : 5;
      uint32_t        RT      : 5;
      uint32_t                : 6;
    } XO;
    // kXEPPCInstrFormatA
    struct {
      uint32_t        Rc      : 1;
      uint32_t        XO      : 5;
      uint32_t        FRC     : 5;
      uint32_t        FRB     : 5;
      uint32_t        FRA     : 5;
      uint32_t        FRT     : 5;
      uint32_t                : 6;
    } A;
    // kXEPPCInstrFormatM
    struct {
      uint32_t        Rc      : 1;
      uint32_t        ME      : 5;
      uint32_t        MB      : 5;
      uint32_t        SH      : 5;
      uint32_t        RA      : 5;
      uint32_t        RT      : 5;
      uint32_t                : 6;
    } M;
    // kXEPPCInstrFormatMD
    struct {
      uint32_t        Rc      : 1;
      uint32_t        SH5     : 1;
      uint32_t        idx     : 3;
      uint32_t        MB5     : 1;
      uint32_t        MB      : 5;
      uint32_t        SH      : 5;
      uint32_t        RA      : 5;
      uint32_t        RT      : 5;
      uint32_t                : 6;
    } MD;
    // kXEPPCInstrFormatMDS
    struct {
      uint32_t        Rc      : 1;
      uint32_t        idx     : 4;
      uint32_t        MB5     : 1;
      uint32_t        MB      : 5;
      uint32_t        RB      : 5;
      uint32_t        RA      : 5;
      uint32_t        RT      : 5;
      uint32_t                : 6;
    } MDS;
    // kXEPPCInstrFormatVXA
    struct {
      uint32_t                : 6;
      uint32_t        VC      : 5;
      uint32_t        VB      : 5;
      uint32_t        VA      : 5;
      uint32_t        VD      : 5;
      uint32_t                : 6;
    } VXA;
    // kXEPPCInstrFormatVX
    struct {
      uint32_t                : 11;
      uint32_t        VB      : 5;
      uint32_t        VA      : 5;
      uint32_t        VD      : 5;
      uint32_t                : 6;
    } VX;
    // kXEPPCInstrFormatVXR
    struct {
      uint32_t                : 10;
      uint32_t        Rc      : 1;
      uint32_t        VB      : 5;
      uint32_t        VA      : 5;
      uint32_t        VD      : 5;
      uint32_t                : 6;
    } VXR;
    // kXEPPCInstrFormatVX128
    struct {
      // VD128 = VD128l | (VD128h << 5)
      // VA128 = VA128l | (VA128h << 5) | (VA128H << 6)
      // VB128 = VB128l | (VB128h << 5)
      uint32_t        VB128h  : 2;
      uint32_t        VD128h  : 2;
      uint32_t                : 1;
      uint32_t        VA128h  : 1;
      uint32_t                : 4;
      uint32_t        VA128H  : 1;
      uint32_t        VB128l  : 5;
      uint32_t        VA128l  : 5;
      uint32_t        VD128l  : 5;
      uint32_t                : 6;
    } VX128;
    // kXEPPCInstrFormatVX128_1
    struct {
      // VD128 = VD128l | (VD128h << 5)
      uint32_t                : 2;
      uint32_t        VD128h  : 2;
      uint32_t                : 7;
      uint32_t        RB      : 5;
      uint32_t        RA      : 5;
      uint32_t        VD128l  : 5;
      uint32_t                : 6;
    } VX128_1;
    // kXEPPCInstrFormatVX128_2
    struct {
      // VD128 = VD128l | (VD128h << 5)
      // VA128 = VA128l | (VA128h << 5) | (VA128H << 6)
      // VB128 = VB128l | (VB128h << 5)
      uint32_t        VB128h  : 2;
      uint32_t        VD128h  : 2;
      uint32_t                : 1;
      uint32_t        VA128h  : 1;
      uint32_t        VC      : 3;
      uint32_t                : 1;
      uint32_t        VA128H  : 1;
      uint32_t        VB128l  : 5;
      uint32_t        VA128l  : 5;
      uint32_t        VD128l  : 5;
      uint32_t                : 6;
    } VX128_2;
    // kXEPPCInstrFormatVX128_3
    struct {
      // VD128 = VD128l | (VD128h << 5)
      // VB128 = VB128l | (VB128h << 5)
      uint32_t        VB128h  : 2;
      uint32_t        VD128h  : 2;
      uint32_t                : 7;
      uint32_t        VB128l  : 5;
      uint32_t        IMM     : 5;
      uint32_t        VD128l  : 5;
      uint32_t                : 6;
    } VX128_3;
    // kXEPPCInstrFormatVX128_4
    struct {
      // VD128 = VD128l | (VD128h << 5)
      // VB128 = VB128l | (VB128h << 5)
      uint32_t        VB128h  : 2;
      uint32_t        VD128h  : 2;
      uint32_t                : 2;
      uint32_t        z       : 2;
      uint32_t                : 3;
      uint32_t        VB128l  : 5;
      uint32_t        IMM     : 5;
      uint32_t        VD128l  : 5;
      uint32_t                : 6;
    } VX128_4;
    // kXEPPCInstrFormatVX128_5
    struct {
      // VD128 = VD128l | (VD128h << 5)
      // VA128 = VA128l | (VA128h << 5) | (VA128H << 6)
      // VB128 = VB128l | (VB128h << 5)
      uint32_t        VB128h  : 2;
      uint32_t        VD128h  : 2;
      uint32_t                : 1;
      uint32_t        VA128h  : 1;
      uint32_t        SH      : 4;
      uint32_t        VA128H  : 1;
      uint32_t        VB128l  : 5;
      uint32_t        VA128l  : 5;
      uint32_t        VD128l  : 5;
      uint32_t                : 6;
    } VX128_5;
    // kXEPPCInstrFormatVX128_P
    struct {
      // VD128 = VD128l | (VD128h << 5)
      // VB128 = VB128l | (VB128h << 5)
      // PERM = PERMl | (PERMh << 5)
      uint32_t        VB128h  : 2;
      uint32_t        VD128h  : 2;
      uint32_t                : 2;
      uint32_t        PERMh   : 3;
      uint32_t                : 2;
      uint32_t        VB128l  : 5;
      uint32_t        PERMl   : 5;
      uint32_t        VD128l  : 5;
      uint32_t                : 6;
    } VX128_P;
    // kXEPPCInstrFormatVX128_R
    struct {
      // VD128 = VD128l | (VD128h << 5)
      // VA128 = VA128l | (VA128h << 5) | (VA128H << 6)
      // VB128 = VB128l | (VB128h << 5)
      uint32_t        VB128h  : 2;
      uint32_t        VD128h  : 2;
      uint32_t                : 1;
      uint32_t        VA128h  : 1;
      uint32_t        Rc      : 1;
      uint32_t                : 3;
      uint32_t        VA128H  : 1;
      uint32_t        VB128l  : 5;
      uint32_t        VA128l  : 5;
      uint32_t        VD128l  : 5;
      uint32_t                : 6;
    } VX128_R;
    // kXEPPCInstrFormatXDSS
    struct {
    } XDSS;
  };
} InstrData;


typedef struct {
  enum RegisterSet {
    kXER,
    kLR,
    kCTR,
    kCR,    // 0-7
    kFPSCR,
    kGPR,   // 0-31
    kFPR,   // 0-31
    kVMX,   // 0-127
  };

  enum Access {
    kRead       = 1 << 0,
    kWrite      = 1 << 1,
    kReadWrite  = kRead | kWrite,
  };

  RegisterSet set;
  uint32_t    ordinal;
  Access      access;
} InstrRegister;


typedef struct {
  enum OperandType {
    kRegister,
    kImmediate,
  };

  OperandType type;
  const char* display;
  union {
    InstrRegister reg;
    struct {
      bool        is_signed;
      uint64_t    value;
      size_t      width;
    } imm;
  };

  void Dump(std::string& out_str);
} InstrOperand;


class InstrAccessBits {
public:
  InstrAccessBits() :
      spr(0), cr(0), gpr(0), fpr(0),
      vr31_0(0), vr63_32(0), vr95_64(0), vr127_96(0) {
  }

  // Bitmasks derived from the accesses to registers.
  // Format is 2 bits for each register, even bits indicating reads and odds
  // indicating writes.
  uint64_t spr;   // fpcsr/ctr/lr/xer
  uint64_t cr;    // cr7/6/5/4/3/2/1/0
  uint64_t gpr;   // r31-0
  uint64_t fpr;   // f31-0
  uint64_t vr31_0;
  uint64_t vr63_32;
  uint64_t vr95_64;
  uint64_t vr127_96;

  void Clear();
  void Extend(InstrAccessBits& other);
  void MarkAccess(InstrRegister& reg);
  void Dump(std::string& out_str);
};


class InstrDisasm {
public:
  enum Flags {
    kOE   = 1 << 0,
    kRc   = 1 << 1,
    kCA   = 1 << 2,
    kLR   = 1 << 4,
    kFP   = 1 << 5,
    kVMX  = 1 << 6,
  };

  const char*   name;
  const char*   info;
  uint32_t      flags;
  std::vector<InstrOperand> operands;
  std::vector<InstrRegister> special_registers;
  InstrAccessBits access_bits;

  void Init(const char* name, const char* info, uint32_t flags);
  void AddLR(InstrRegister::Access access);
  void AddCTR(InstrRegister::Access access);
  void AddCR(uint32_t bf, InstrRegister::Access access);
  void AddFPSCR(InstrRegister::Access access);
  void AddRegOperand(InstrRegister::RegisterSet set, uint32_t ordinal,
                     InstrRegister::Access access, const char* display = NULL);
  void AddSImmOperand(uint64_t value, size_t width, const char* display = NULL);
  void AddUImmOperand(uint64_t value, size_t width, const char* display = NULL);
  int Finish();

  void Dump(std::string& out_str, size_t pad = 13);
};


typedef int (*InstrDisassembleFn)(InstrData& i, InstrDisasm& d);
typedef void* InstrEmitFn;


class InstrType {
public:
  uint32_t    opcode;
  uint32_t    opcode_mask;    // Only used for certain opcodes (altivec, etc).
  uint32_t    format;         // xe_ppc_instr_format_e
  uint32_t    type;           // xe_ppc_instr_type_e
  uint32_t    flags;          // xe_ppc_instr_flag_e
  char        name[16];

  InstrDisassembleFn disassemble;
  InstrEmitFn        emit;
};

InstrType* GetInstrType(uint32_t code);
int RegisterInstrDisassemble(uint32_t code, InstrDisassembleFn disassemble);
int RegisterInstrEmit(uint32_t code, InstrEmitFn emit);


}  // namespace ppc
}  // namespace frontend
}  // namespace alloy


#endif  // ALLOY_FRONTEND_PPC_PPC_INSTR_H_

