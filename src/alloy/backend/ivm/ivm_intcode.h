/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_IVM_INTCODE_H_
#define ALLOY_BACKEND_IVM_INTCODE_H_

#include <alloy/core.h>

#include <alloy/hir/instr.h>
#include <alloy/hir/opcodes.h>

namespace alloy { namespace runtime { class ThreadState; } }


namespace alloy {
namespace backend {
namespace ivm {


typedef union {
  int8_t    i8;
  uint8_t   u8;
  int16_t   i16;
  uint16_t  u16;
  int32_t   i32;
  uint32_t  u32;
  int64_t   i64;
  uint64_t  u64;
  float     f32;
  double    f64;
  vec128_t  v128;
} Register;


typedef struct {
  Register*     rf;
  uint8_t*      context;
  uint8_t*      membase;
  int8_t        did_carry;
  runtime::ThreadState* thread_state;
  uint64_t      return_address;
} IntCodeState;


struct IntCode_s;
typedef uint32_t (*IntCodeFn)(
    IntCodeState& ics, const struct IntCode_s* i);

#define IA_RETURN 0xA0000000
#define IA_NEXT   0xB0000000


typedef struct IntCode_s {
  IntCodeFn intcode_fn;
  uint16_t  flags;

  uint32_t  dest_reg;
  union {
    struct {
      uint32_t  src1_reg;
      uint32_t  src2_reg;
      uint32_t  src3_reg;
      // <4 bytes available>
    };
    struct {
      Register  constant;
    };
  };

  // debugging info/etc
} IntCode;


typedef struct LabelRef_s {
  hir::Label* label;
  IntCode*    instr;
  LabelRef_s* next;
} LabelRef;


typedef struct {
  uint32_t  register_count;
  size_t    intcode_count;
  Arena*    intcode_arena;
  Arena*    scratch_arena;
  LabelRef* label_ref_head;
} TranslationContext;


int TranslateIntCodes(TranslationContext& ctx, hir::Instr* i);


}  // namespace ivm
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_IVM_INTCODE_H_
