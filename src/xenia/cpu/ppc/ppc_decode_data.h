/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_PPC_DECODE_DATA_H_
#define XENIA_CPU_PPC_PPC_DECODE_DATA_H_

#include <cstdint>

#include "xenia/base/math.h"
#include "xenia/base/string_buffer.h"
#include "xenia/cpu/ppc/ppc_instr.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"

namespace xe {
namespace cpu {
namespace ppc {

constexpr int64_t XEEXTS16(uint32_t v) { return (int64_t)((int16_t)v); }
constexpr int64_t XEEXTS26(uint32_t v) {
  return (int64_t)(v & 0x02000000 ? (int32_t)v | 0xFC000000 : (int32_t)(v));
}
constexpr uint64_t XEEXTZ16(uint32_t v) { return (uint64_t)((uint16_t)v); }
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

struct PPCDecodeData {
  struct FormatSC {
    uint32_t LEV() const { return bits_.LEV; }

   private:
	   XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t : 5;
        uint32_t LEV : 7;
        uint32_t : 20;
      } bits_;
    };
  };
  struct FormatD {
    uint32_t RT() const { return bits_.RT; }
    uint32_t RD() const { return RT(); }
    uint32_t RS() const { return RT(); }
    uint32_t FD() const { return RT(); }
    uint32_t FS() const { return RT(); }
    uint32_t TO() const { return RT(); }
    uint32_t RA() const { return bits_.RA; }
    uint32_t RA0() const { return RA(); }
    uint32_t DS() const { return bits_.DS; }
    int32_t d() const { return static_cast<int32_t>(XEEXTS16(DS())); }
    int32_t SIMM() const { return d(); }
    uint32_t UIMM() const { return static_cast<uint32_t>(XEEXTZ16(DS())); }

    uint32_t CRFD() const { return bits_.RT >> 2; }
    uint32_t L() const { return bits_.RT & 0x1; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t DS : 16;
        uint32_t RA : 5;
        uint32_t RT : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatDS {
    uint32_t RT() const { return bits_.RT; }
    uint32_t RD() const { return RT(); }
    uint32_t RS() const { return RT(); }
    uint32_t RA() const { return bits_.RA; }
    uint32_t RA0() const { return RA(); }
    uint32_t DS() const { return bits_.DS; }
    int32_t ds() const { return static_cast<int32_t>(XEEXTS16(DS() << 2)); }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t : 2;
        uint32_t DS : 14;
        uint32_t RA : 5;
        uint32_t RT : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatB {
    uint32_t BO() const { return bits_.BO; }
    uint32_t BI() const { return bits_.BI; }
    uint32_t BD() const { return bits_.BD; }
    uint32_t ADDR() const {
      return static_cast<uint32_t>(XEEXTS16(BD() << 2)) + (AA() ? 0 : address_);
    }
    bool AA() const { return bits_.AA ? true : false; }
    bool LK() const { return bits_.LK ? true : false; }

   private:
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t LK : 1;
        uint32_t AA : 1;
        uint32_t BD : 14;
        uint32_t BI : 5;
        uint32_t BO : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatI {
    uint32_t LI() const { return bits_.LI; }
    uint32_t ADDR() const {
      return static_cast<uint32_t>(XEEXTS26(LI() << 2)) + (AA() ? 0 : address_);
    }
    bool AA() const { return bits_.AA ? true : false; }
    bool LK() const { return bits_.LK ? true : false; }

   private:
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t LK : 1;
        uint32_t AA : 1;
        uint32_t LI : 24;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatX {
    uint32_t RT() const { return bits_.RT; }
    uint32_t RD() const { return RT(); }
    uint32_t RS() const { return RT(); }
    uint32_t FD() const { return RT(); }
    uint32_t FS() const { return RT(); }
    uint32_t VD() const { return RT(); }
    uint32_t VS() const { return RT(); }
    uint32_t TO() const { return RT(); }
    uint32_t RA() const { return bits_.RA; }
    uint32_t RA0() const { return RA(); }
    uint32_t FA() const { return RA(); }
    uint32_t RB() const { return bits_.RB; }
    uint32_t FB() const { return RB(); }
    uint32_t SH() const { return RB(); }
    uint32_t IMM() const { return RB(); }
    bool Rc() const { return bits_.Rc ? true : false; }

    uint32_t CRFD() const { return bits_.RT >> 2; }
    uint32_t L() const { return bits_.RT & 0x1; }
    uint32_t CRFS() const { return bits_.RA >> 2; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t Rc : 1;
        uint32_t : 10;
        uint32_t RB : 5;
        uint32_t RA : 5;
        uint32_t RT : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatXL {
    uint32_t BO() const { return bits_.BO; }
    uint32_t CRBD() const { return BO(); }
    uint32_t BI() const { return bits_.BI; }
    uint32_t CRBA() const { return BI(); }
    uint32_t BB() const { return bits_.BB; }
    uint32_t CRBB() const { return BB(); }
    bool LK() const { return bits_.LK ? true : false; }

    uint32_t CRFD() const { return CRBD() >> 2; }
    uint32_t CRFS() const { return CRBA() >> 2; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t LK : 1;
        uint32_t : 10;
        uint32_t BB : 5;
        uint32_t BI : 5;
        uint32_t BO : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatXFX {
    uint32_t RT() const { return bits_.RT; }
    uint32_t RD() const { return RT(); }
    uint32_t RS() const { return RT(); }
    uint32_t SPR() const { return bits_.SPR; }
    uint32_t TBR() const {
      return ((bits_.SPR & 0x1F) << 5) | ((bits_.SPR >> 5) & 0x1F);
    }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t : 1;
        uint32_t : 10;
        uint32_t SPR : 10;
        uint32_t RT : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatXFL {
    bool L() const { return bits_.L ? true : false; }
    uint32_t FM() const { return bits_.FM; }
    bool W() const { return bits_.W ? true : false; }
    uint32_t RB() const { return bits_.RB; }
    uint32_t FB() const { return RB(); }
    bool Rc() const { return bits_.Rc ? true : false; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t Rc : 1;
        uint32_t : 10;
        uint32_t RB : 5;
        uint32_t W : 1;
        uint32_t FM : 8;
        uint32_t L : 1;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatXS {
    uint32_t RT() const { return bits_.RT; }
    uint32_t RS() const { return RT(); }
    uint32_t RA() const { return bits_.RA; }
    uint32_t SH() const { return (bits_.SH5 << 5) | bits_.SH; }
    bool Rc() const { return bits_.Rc ? true : false; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t Rc : 1;
        uint32_t SH5 : 1;
        uint32_t : 9;
        uint32_t SH : 5;
        uint32_t RA : 5;
        uint32_t RT : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatXO {
    uint32_t RT() const { return bits_.RT; }
    uint32_t RD() const { return RT(); }
    uint32_t RA() const { return bits_.RA; }
    uint32_t RB() const { return bits_.RB; }
    bool OE() const { return bits_.OE ? true : false; }
    bool Rc() const { return bits_.Rc ? true : false; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t Rc : 1;
        uint32_t : 9;
        uint32_t OE : 1;
        uint32_t RB : 5;
        uint32_t RA : 5;
        uint32_t RT : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatA {
    uint32_t FT() const { return bits_.FT; }
    uint32_t FD() const { return FT(); }
    uint32_t FS() const { return FT(); }
    uint32_t FA() const { return bits_.FA; }
    uint32_t FB() const { return bits_.FB; }
    uint32_t FC() const { return bits_.FC; }
    uint32_t XO() const { return bits_.XO; }
    bool Rc() const { return bits_.Rc ? true : false; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t Rc : 1;
        uint32_t XO : 5;
        uint32_t FC : 5;
        uint32_t FB : 5;
        uint32_t FA : 5;
        uint32_t FT : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatM {
    uint32_t RT() const { return bits_.RT; }
    uint32_t RS() const { return RT(); }
    uint32_t RA() const { return bits_.RA; }
    uint32_t SH() const { return bits_.SH; }
    uint32_t RB() const { return SH(); }
    uint32_t MB() const { return bits_.MB; }
    uint32_t ME() const { return bits_.ME; }
    bool Rc() const { return bits_.Rc ? true : false; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t Rc : 1;
        uint32_t ME : 5;
        uint32_t MB : 5;
        uint32_t SH : 5;
        uint32_t RA : 5;
        uint32_t RT : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatMD {
    uint32_t RT() const { return bits_.RT; }
    uint32_t RS() const { return RT(); }
    uint32_t RA() const { return bits_.RA; }
    uint32_t SH() const { return (bits_.SH5 << 5) | bits_.SH; }
    uint32_t MB() const { return (bits_.MB5 << 5) | bits_.MB; }
    uint32_t ME() const { return MB(); }
    bool Rc() const { return bits_.Rc ? true : false; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t Rc : 1;
        uint32_t SH5 : 1;
        uint32_t : 3;
        uint32_t MB5 : 1;
        uint32_t MB : 5;
        uint32_t SH : 5;
        uint32_t RA : 5;
        uint32_t RT : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatMDS {
    uint32_t RT() const { return bits_.RT; }
    uint32_t RS() const { return RT(); }
    uint32_t RA() const { return bits_.RA; }
    uint32_t RB() const { return bits_.RB; }
    uint32_t MB() const { return (bits_.MB5 << 5) | bits_.MB; }
    uint32_t ME() const { return MB(); }
    bool Rc() const { return bits_.Rc ? true : false; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t Rc : 1;
        uint32_t : 4;
        uint32_t MB5 : 1;
        uint32_t MB : 5;
        uint32_t RB : 5;
        uint32_t RA : 5;
        uint32_t RT : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatVX {
    uint32_t VD() const { return bits_.VD; }
    uint32_t VA() const { return bits_.VA; }
    uint32_t VB() const { return bits_.VB; }
    uint32_t UIMM() const { return VA(); }
    int32_t SIMM() const { return static_cast<int32_t>(XEEXTS16(VA())); }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t : 11;
        uint32_t VB : 5;
        uint32_t VA : 5;
        uint32_t VD : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatVC {
    uint32_t VD() const { return bits_.VD; }
    uint32_t VA() const { return bits_.VA; }
    uint32_t VB() const { return bits_.VB; }
    bool Rc() const { return bits_.Rc ? true : false; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t : 10;
        uint32_t Rc : 1;
        uint32_t VB : 5;
        uint32_t VA : 5;
        uint32_t VD : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatVA {
    uint32_t VD() const { return bits_.VD; }
    uint32_t VA() const { return bits_.VA; }
    uint32_t VB() const { return bits_.VB; }
    uint32_t VC() const { return bits_.VC; }
    uint32_t SHB() const { return VC() & 0xF; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t : 6;
        uint32_t VC : 5;
        uint32_t VB : 5;
        uint32_t VA : 5;
        uint32_t VD : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatVX128 {
    uint32_t VD() const { return bits_.VD128l | (bits_.VD128h << 5); }
    uint32_t VA() const {
      return bits_.VA128l | (bits_.VA128h << 5) | (bits_.VA128H << 6);
    }
    uint32_t VB() const { return bits_.VB128l | (bits_.VB128h << 5); }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t VB128h : 2;
        uint32_t VD128h : 2;
        uint32_t : 1;
        uint32_t VA128h : 1;
        uint32_t : 4;
        uint32_t VA128H : 1;
        uint32_t VB128l : 5;
        uint32_t VA128l : 5;
        uint32_t VD128l : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatVX128_1 {
    uint32_t VD() const { return bits_.VD128l | (bits_.VD128h << 5); }
    uint32_t VS() const { return VD(); }
    uint32_t RA() const { return bits_.RA; }
    uint32_t RA0() const { return RA(); }
    uint32_t RB() const { return bits_.RB; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t : 2;
        uint32_t VD128h : 2;
        uint32_t : 7;
        uint32_t RB : 5;
        uint32_t RA : 5;
        uint32_t VD128l : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatVX128_2 {
    uint32_t VD() const { return bits_.VD128l | (bits_.VD128h << 5); }
    uint32_t VA() const {
      return bits_.VA128l | (bits_.VA128h << 5) | (bits_.VA128H << 6);
    }
    uint32_t VB() const { return bits_.VB128l | (bits_.VB128h << 5); }
    uint32_t VC() const { return bits_.VC; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t VB128h : 2;
        uint32_t VD128h : 2;
        uint32_t : 1;
        uint32_t VA128h : 1;
        uint32_t VC : 3;
        uint32_t : 1;
        uint32_t VA128H : 1;
        uint32_t VB128l : 5;
        uint32_t VA128l : 5;
        uint32_t VD128l : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatVX128_3 {
    uint32_t VD() const { return bits_.VD128l | (bits_.VD128h << 5); }
    uint32_t VB() const { return bits_.VB128l | (bits_.VB128h << 5); }
    uint32_t UIMM() const { return bits_.UIMM; }
    int32_t SIMM() const { return static_cast<int32_t>(XEEXTS16(bits_.UIMM)); }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t VB128h : 2;
        uint32_t VD128h : 2;
        uint32_t : 7;
        uint32_t VB128l : 5;
        uint32_t UIMM : 5;
        uint32_t VD128l : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatVX128_4 {
    uint32_t VD() const { return bits_.VD128l | (bits_.VD128h << 5); }
    uint32_t VB() const { return bits_.VB128l | (bits_.VB128h << 5); }
    uint32_t IMM() const { return bits_.IMM; }
    uint32_t z() const { return bits_.z; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t VB128h : 2;
        uint32_t VD128h : 2;
        uint32_t : 2;
        uint32_t z : 2;
        uint32_t : 3;
        uint32_t VB128l : 5;
        uint32_t IMM : 5;
        uint32_t VD128l : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatVX128_5 {
    uint32_t VD() const { return bits_.VD128l | (bits_.VD128h << 5); }
    uint32_t VA() const {
      return bits_.VA128l | (bits_.VA128h << 5) | (bits_.VA128H << 6);
    }
    uint32_t VB() const { return bits_.VB128l | (bits_.VB128h << 5); }
    uint32_t SH() const { return bits_.SH; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t VB128h : 2;
        uint32_t VD128h : 2;
        uint32_t : 1;
        uint32_t VA128h : 1;
        uint32_t SH : 4;
        uint32_t VA128H : 1;
        uint32_t VB128l : 5;
        uint32_t VA128l : 5;
        uint32_t VD128l : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatVX128_R {
    uint32_t VD() const { return bits_.VD128l | (bits_.VD128h << 5); }
    uint32_t VA() const {
      return bits_.VA128l | (bits_.VA128h << 5) | (bits_.VA128H << 6);
    }
    uint32_t VB() const { return bits_.VB128l | (bits_.VB128h << 5); }
    bool Rc() const { return bits_.Rc ? true : false; }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t VB128h : 2;
        uint32_t VD128h : 2;
        uint32_t : 1;
        uint32_t VA128h : 1;
        uint32_t Rc : 1;
        uint32_t : 3;
        uint32_t VA128H : 1;
        uint32_t VB128l : 5;
        uint32_t VA128l : 5;
        uint32_t VD128l : 5;
        uint32_t : 6;
      } bits_;
    };
  };
  struct FormatVX128_P {
    uint32_t VD() const { return bits_.VD128l | (bits_.VD128h << 5); }
    uint32_t VB() const { return bits_.VB128l | (bits_.VB128h << 5); }
    uint32_t UIMM() const { return bits_.PERMl | (bits_.PERMh << 5); }

   private:
    XE_MAYBE_UNUSED
    uint32_t address_;
    union {
      uint32_t value_;
      struct {
        uint32_t VB128h : 2;
        uint32_t VD128h : 2;
        uint32_t : 2;
        uint32_t PERMh : 3;
        uint32_t : 2;
        uint32_t VB128l : 5;
        uint32_t PERMl : 5;
        uint32_t VD128l : 5;
        uint32_t : 6;
      } bits_;
    };
  };

  union {
    struct {
      uint32_t address;
      uint32_t code;
    };
    FormatSC SC;
    FormatD D;
    FormatDS DS;
    FormatB B;
    FormatI I;
    FormatX X;
    FormatXL XL;
    FormatXFX XFX;
    FormatXFL XFL;
    FormatXS XS;
    FormatXO XO;
    FormatA A;
    FormatM M;
    FormatMD MD;
    FormatMDS MDS;
    FormatX DCBZ;
    FormatVX VX;
    FormatVC VC;
    FormatVA VA;
    FormatVX128 VX128;
    FormatVX128_1 VX128_1;
    FormatVX128_2 VX128_2;
    FormatVX128_3 VX128_3;
    FormatVX128_4 VX128_4;
    FormatVX128_5 VX128_5;
    FormatVX128_R VX128_R;
    FormatVX128_P VX128_P;
  };
};

}  // namespace ppc
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PPC_PPC_DECODE_DATA_H_
