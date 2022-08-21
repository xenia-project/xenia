#ifndef XENIA_CPU_BACKEND_X64_X64_AMDFX_EXTENSIONS_H_
#define XENIA_CPU_BACKEND_X64_X64_AMDFX_EXTENSIONS_H_

#include <stdio.h>
#include <string.h>
#include <string>

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {
namespace amdfx {
enum xopcodemap_e : unsigned char {
  XOPCODE_HAS_IMMBYTE = 0x8,
  XOPCODE_NO_IMMBYTE = 0x9
};

// base opcodes, without their size specified
enum xopcode_e : unsigned char {
  xop_VFRCZPD = 0x81,
  xop_VFRCZPS = 0x80,
  xop_VFRCZSD = 0x83,
  xop_VFRCZSS = 0x82,
  xop_VPCMOV = 0xA2,
  xop_VPCOMB = 0xCC,
  xop_VPCOMD = 0xCE,
  xop_VPCOMQ = 0xCF,
  xop_VPCOMUB = 0xEC,
  xop_VPCOMUD = 0xEE,
  xop_VPCOMUQ = 0xEF,
  xop_VPCOMUW = 0xED,
  xop_VPCOMW = 0xCD,
  xop_VPERMIL2PD = 0x49,
  xop_VPERMIL2PS = 0x48,
  xop_VPHADDBD = 0xC2,
  xop_VPHADDBQ = 0xC3,
  xop_VPHADDBW = 0xC1,
  xop_VPHADDDQ = 0xCB,
  xop_VPHADDUBD = 0xD2,
  xop_VPHADDUBQ = 0xD3,
  xop_VPHADDUBW = 0xD1,
  xop_VPHADDUDQ = 0xDB,
  xop_VPHADDUWD = 0xD6,
  xop_VPHADDUWQ = 0xD7,
  xop_VPHADDWD = 0xC6,
  xop_VPHADDWQ = 0xC7,
  xop_VPHSUBBW = 0xE1,
  xop_VPHSUBDQ = 0xE3,
  xop_VPHSUBWD = 0xE2,
  xop_VPMACSDD = 0x9E,
  xop_VPMACSDQH = 0x9F,
  xop_VPMACSDQL = 0x97,
  xop_VPMACSSDD = 0x8E,
  xop_VPMACSSDQH = 0x8F,
  xop_VPMACSSDQL = 0x87,
  xop_VPMACSSWD = 0x86,
  xop_VPMACSSWW = 0x85,
  xop_VPMACSWD = 0x96,
  xop_VPMACSWW = 0x95,
  xop_VPMADCSSWD = 0xA6,
  xop_VPMADCSWD = 0xB6,
  xop_VPPERM = 0xA3,
  xop_VPROTB = 0x90,
  xop_VPROTBI = 0xC0,  // imm version
  xop_VPROTD = 0x92,
  xop_VPROTDI = 0xC2,
  xop_VPROTQ = 0x93,
  xop_VPROTQI = 0xC3,
  xop_VPROTW = 0x91,
  xop_VPROTWI = 0xC1,
  xop_VPSHAB = 0x98,
  xop_VPSHAD = 0x9A,
  xop_VPSHAQ = 0x9B,
  xop_VPSHAW = 0x99,
  xop_VPSHLB = 0x94,
  xop_VPSHLD = 0x96,
  xop_VPSHLQ = 0x97,
  xop_VPSHLW = 0x95,

};

enum xop_iop_e : unsigned char {
  XOP_BYTE = 0,
  XOP_WORD = 1,
  XOP_DOUBLEWORD = 2,
  XOP_QUADWORD = 3
};

enum xop_fop_e : unsigned char {
  XOP_PS = 0,
  XOP_PD = 1,
  XOP_SS = 2,
  XOP_SD = 3
};
class xop_byte1_t {
 public:
  union {
    // informative names
    struct {
      /*
              A five bit field encoding a one- or two-byte opcode prefix.
      */
      unsigned char opcode_map_select : 5;
      /*
              This bit provides a one-bit extension of either the ModRM.r/m
         field to specify a GPR or XMM register or to the SIB base field to
         specify a GPR. This permits access to 16 registers. In 32-bit protected
         and compatibility modes, this bit is ignored. This bit is the
         bit-inverted equivalent of the REX.B bit and is available only in the
         3-byte prefix format.
      */
      unsigned char inv_1bit_ext_modrm_or_sib : 1;
      /*
              This bit provides a one bit extension of the SIB.index field in
         64-bit mode, permitting access to 16 YMM/XMM and GPR registers. In
         32-bit protected and compatibility modes, this bit must be set to 1.
         This bit is the bit-inverted equivalent of the REX.X bit
      */
      unsigned char inv_1bit_ext_sib_index : 1;
      /*
              This bit provides a one bit extension of the ModRM.reg field in
         64-bit mode, permitting access to all 16 YMM/XMM and GPR registers. In
         32-bit protected and compatibility modes, this bit must be set to 1.
         This bit is the bit-inverted equivalent of the REX.R bit.
      */
      unsigned char inv_1bit_ext_modrm_reg_field : 1;
    };
    // amd manual names
    struct {
      unsigned char mmmmm : 5;
      unsigned char B : 1;
      unsigned char X : 1;
      unsigned char R : 1;
    };
    unsigned char encoded;
  };
};

class xop_byte2_t {
 public:
  union {
    struct {
      unsigned char
          implied_66f2f3_ext : 2;  // 0 = no implied, 1 = 66, 2 = F3, 3 = F2
      unsigned char vector_length : 1;
      unsigned char source_or_dest_reg_specifier_inverted_1s_compl : 4;
      unsigned char scalar_reg_size_override_special : 1;
    };
    // amd manual names

    struct {
      unsigned char pp : 2;  // presumably 0 = no implied, 1 = 66, 2 = F2, 3 =
                             // F3
      unsigned char L : 1;
      unsigned char vvvv : 4;  // src1 for four operand form
      unsigned char W : 1;
    };
    unsigned char encoded;
  };
};

class xop_opcode_byte_t {
 public:
  union {
    struct {
      xop_fop_e float_datatype : 2;
      unsigned char __unused0 : 6;
    };

    struct {
      xop_iop_e int_datatype : 2;
      unsigned char __unused1 : 6;
    };

    struct {
      unsigned char oes : 2;
      unsigned char opcode : 6;
    };
    unsigned char encoded;
  };
};

class modrm_byte_t {
 public:
  union {
    struct {
      unsigned char rm : 3;
      unsigned char mod : 5;  // 4 opnd form dest reg
    };
    unsigned char encoded;
  };
};

#pragma pack(push, 1)
class xop_t {
 public:
  unsigned char imm_8F;  // always 0x8F
  xop_byte1_t byte1;
  xop_byte2_t byte2;
  xop_opcode_byte_t opcode;
  modrm_byte_t modrm;
  unsigned char imm8;

  xop_t() : imm_8F(0x8F) {
    byte1.encoded = 0;
    byte2.encoded = 0;
    opcode.encoded = 0;
    modrm.encoded = 0;
  }

  unsigned AssembledSize() const {
    if (byte1.opcode_map_select == XOPCODE_NO_IMMBYTE) {
      return 5;
    } else {
      return 6;
    }
  }

  template <typename TCall>
  void ForeachByte(TCall&& cb) {
    cb(imm_8F);
    cb(byte1.encoded);
    cb(byte2.encoded);
    cb(opcode.encoded);
    cb(modrm.encoded);
    if (AssembledSize() == 6) {
      cb(imm8);
    }
  }
};
#pragma pack(pop)

static void xop_set_fouroperand_form(xop_t& xop, unsigned xmmidx_dest,
                                     unsigned xmmidx_src1, unsigned xmmidx_src2,
                                     unsigned xmmidx_src3, xopcode_e opcode,
                                     bool has_immbyte = true) {
  xop.opcode.encoded = opcode;
  xop.byte1.encoded = 0xe8;
  if (has_immbyte) {
    xop.byte1.opcode_map_select = XOPCODE_HAS_IMMBYTE;
  } else {
    xop.byte1.opcode_map_select = XOPCODE_NO_IMMBYTE;
  }
  xop.imm8 = xmmidx_src3 << 4;

  xop.modrm.rm = xmmidx_src2 & 0b111;
  xop.byte1.inv_1bit_ext_modrm_reg_field = (xmmidx_dest >> 3) ^ 1;
  xop.byte1.inv_1bit_ext_modrm_or_sib = (xmmidx_src2 >> 3) ^ 1;
  xop.byte2.vvvv = ~xmmidx_src1;
  xop.modrm.encoded |= 0xC0;
  xop.modrm.mod |= xmmidx_dest & 0b111;
}

enum class xopcompare_e : uint32_t {
  LT = 0b000,
  LTE = 0b001,
  GT = 0b010,
  GTE = 0b011,
  EQ = 0b100,
  NEQ = 0b101,
  FALSEY = 0b110,  // there doesnt seem to be much in the way of documentation
                   // for these two
  TRUTHEY = 0b111
};

namespace operations {
#define SIMPLE_FOUROPERAND(funcname, opcode)                                  \
  static xop_t funcname(unsigned destidx, unsigned src1idx, unsigned src2idx, \
                        unsigned src3idx) {                                   \
    xop_t result{};                                                           \
    xop_set_fouroperand_form(result, destidx, src1idx, src2idx, src3idx,      \
                             opcode, true);                                   \
    return result;                                                            \
  }

SIMPLE_FOUROPERAND(vpcmov, xop_VPCMOV)

SIMPLE_FOUROPERAND(vpperm, xop_VPPERM)

#define COMPAREFUNC(name, opcode)                                    \
  static xop_t name(unsigned dst, unsigned src1, unsigned src2,      \
                    xopcompare_e imm8) {                             \
    xop_t xop;                                                       \
    xop_set_fouroperand_form(xop, dst, src1, src2, 0, opcode, true); \
    xop.imm8 = static_cast<uint8_t>(static_cast<uint32_t>(imm8));    \
    return xop;                                                      \
  }

COMPAREFUNC(vpcomb, xop_VPCOMB)
COMPAREFUNC(vpcomub, xop_VPCOMUB)
COMPAREFUNC(vpcomw, xop_VPCOMW)
COMPAREFUNC(vpcomuw, xop_VPCOMUW)
COMPAREFUNC(vpcomd, xop_VPCOMD)
COMPAREFUNC(vpcomud, xop_VPCOMUD)
COMPAREFUNC(vpcomq, xop_VPCOMQ)
COMPAREFUNC(vpcomuq, xop_VPCOMUQ)

#define SIMPLE_THREEOPERAND(funcname, opcode)                              \
  static xop_t funcname(unsigned destidx, unsigned src1idx,                \
                        unsigned src2idx) {                                \
    xop_t result{};                                                        \
    xop_set_fouroperand_form(result, destidx, src1idx, src2idx, 0, opcode, \
                             false);                                       \
    return result;                                                         \
  }

SIMPLE_THREEOPERAND(vprotb, xop_VPROTB)
SIMPLE_THREEOPERAND(vprotw, xop_VPROTW)
SIMPLE_THREEOPERAND(vprotd, xop_VPROTD)
SIMPLE_THREEOPERAND(vprotq, xop_VPROTQ)

SIMPLE_THREEOPERAND(vpshab, xop_VPSHAB)
SIMPLE_THREEOPERAND(vpshaw, xop_VPSHAW)
SIMPLE_THREEOPERAND(vpshad, xop_VPSHAD)
SIMPLE_THREEOPERAND(vpshaq, xop_VPSHAQ)


SIMPLE_THREEOPERAND(vpshlb, xop_VPSHLB)
SIMPLE_THREEOPERAND(vpshlw, xop_VPSHLW)
SIMPLE_THREEOPERAND(vpshld, xop_VPSHLD)
SIMPLE_THREEOPERAND(vpshlq, xop_VPSHLQ)

#undef SIMPLE_THREEOPERAND
#undef SIMPLE_FOUROPERAND
#undef COMPAREFUNC
}  // namespace operations

}  // namespace amdfx
}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_X64_X64_AMDFX_EXTENSIONS_H_
