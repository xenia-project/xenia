/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_INSTR_H_
#define XENIA_CPU_PPC_INSTR_H_

#include <xenia/common.h>


typedef enum {
  kXEPPCInstrFormatI    = 0,
  kXEPPCInstrFormatB    = 1,
  kXEPPCInstrFormatSC   = 2,
  kXEPPCInstrFormatD    = 3,
  kXEPPCInstrFormatDS   = 4,
  kXEPPCInstrFormatX    = 5,
  kXEPPCInstrFormatXL   = 6,
  kXEPPCInstrFormatXFX  = 7,
  kXEPPCInstrFormatXFL  = 8,
  kXEPPCInstrFormatXS   = 9,
  kXEPPCInstrFormatXO   = 10,
  kXEPPCInstrFormatA    = 11,
  kXEPPCInstrFormatM    = 12,
  kXEPPCInstrFormatMD   = 13,
  kXEPPCInstrFormatMDS  = 14,
  kXEPPCInstrFormatVA   = 15,
  kXEPPCInstrFormatVX   = 16,
  kXEPPCInstrFormatVXR  = 17,
} xe_ppc_instr_format_e;

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


struct xe_ppc_instr_type;
typedef struct xe_ppc_instr_type xe_ppc_instr_type_t;


typedef struct {
  xe_ppc_instr_type_t *type;
  uint32_t            address;

  union {
    uint32_t          code;

    // kXEPPCInstrFormatI
    struct {
      uint32_t        LK      : 1;
      uint32_t        AA      : 1;
      uint32_t        LI      : 24;
      uint32_t        OPCD    : 6;
    } I;
    // kXEPPCInstrFormatB
    struct {
      uint32_t        LK      : 1;
      uint32_t        AA      : 1;
      uint32_t        BD      : 14;
      uint32_t        BI      : 5;
      uint32_t        BO      : 5;
      uint32_t        OPCD    : 6;
    } B;

    // kXEPPCInstrFormatSC
    struct {
      uint32_t        SIMM    : 16;
      uint32_t        A       : 5;
      uint32_t        D       : 5;
      uint32_t        OPCD    : 6;
    } D;
    // kXEPPCInstrFormatDS
    struct {
      uint32_t                : 2;
      uint32_t        ds      : 14;
      uint32_t        A       : 5;
      uint32_t        S       : 5;
      uint32_t        OPCD    : 6;
    } DS;
    // kXEPPCInstrFormatX
    // kXEPPCInstrFormatXL
    struct {
      uint32_t                : 1;
      uint32_t                : 10;
      uint32_t        spr     : 10;
      uint32_t        D       : 5;
      uint32_t        OPCD    : 6;
    } XFX;
    // kXEPPCInstrFormatXFL
    // kXEPPCInstrFormatXS
    // kXEPPCInstrFormatXO
    // kXEPPCInstrFormatA
    // kXEPPCInstrFormatM
    // kXEPPCInstrFormatMD
    // kXEPPCInstrFormatMDS
    // kXEPPCInstrFormatVA
    // kXEPPCInstrFormatVX
    // kXEPPCInstrFormatVXR
  } data;
} xe_ppc_instr_t;

typedef struct {
  xe_ppc_instr_t      instr;

  // TODO(benvanik): registers changed, special bits, etc
} xe_ppc_dec_instr_t;

typedef int (*xe_ppc_emit_fn)(/* emit context */ xe_ppc_instr_t *instr);

struct xe_ppc_instr_type {
  uint32_t            opcode;
  uint32_t            format;   // xe_ppc_instr_format_e
  uint32_t            type;     // xe_ppc_instr_type_e
  uint32_t            flags;    // xe_ppc_instr_flag_e
  char                name[16];

  xe_ppc_emit_fn      emit;
};

xe_ppc_instr_type_t *xe_ppc_get_instr_type(uint32_t code);
int xe_ppc_register_instr_emit(uint32_t code, xe_ppc_emit_fn emit);

#endif  // XENIA_CPU_PPC_INSTR_H_

