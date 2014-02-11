/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_HIR_INSTR_H_
#define ALLOY_HIR_INSTR_H_

#include <alloy/core.h>
#include <alloy/hir/opcodes.h>
#include <alloy/hir/value.h>


namespace alloy { namespace runtime { class FunctionInfo; } }


namespace alloy {
namespace hir {

class Block;
class Label;

enum SignatureType {
  SIG_TYPE_X      = 0,
  SIG_TYPE_I8     = 1,
  SIG_TYPE_I16    = 2,
  SIG_TYPE_I32    = 3,
  SIG_TYPE_I64    = 4,
  SIG_TYPE_F32    = 5,
  SIG_TYPE_F64    = 6,
  SIG_TYPE_V128   = 7,
  SIG_TYPE_C      = (1 << 3),
  SIG_TYPE_I8C    = SIG_TYPE_C | SIG_TYPE_I8,
  SIG_TYPE_I16C   = SIG_TYPE_C | SIG_TYPE_I16,
  SIG_TYPE_I32C   = SIG_TYPE_C | SIG_TYPE_I32,
  SIG_TYPE_I64C   = SIG_TYPE_C | SIG_TYPE_I64,
  SIG_TYPE_F32C   = SIG_TYPE_C | SIG_TYPE_F32,
  SIG_TYPE_F64C   = SIG_TYPE_C | SIG_TYPE_F64,
  SIG_TYPE_V128C  = SIG_TYPE_C | SIG_TYPE_V128,
  SIG_TYPE_IGNORE = 0xFF,
};

class Instr {
public:
  Block*    block;
  Instr*    next;
  Instr*    prev;

  const OpcodeInfo* opcode;
  uint16_t  flags;
  uint32_t  ordinal;

  typedef union {
    runtime::FunctionInfo* symbol_info;
    Label*    label;
    Value*    value;
    uint64_t  offset;
  } Op;

  Value*  dest;
  Op      src1;
  Op      src2;
  Op      src3;

  Value::Use* src1_use;
  Value::Use* src2_use;
  Value::Use* src3_use;

  void set_src1(Value* value);
  void set_src2(Value* value);
  void set_src3(Value* value);

  bool Match(SignatureType dest = SIG_TYPE_X,
             SignatureType src1 = SIG_TYPE_X,
             SignatureType src2 = SIG_TYPE_X,
             SignatureType src3 = SIG_TYPE_X) const;

  void MoveBefore(Instr* other);
  void Replace(const OpcodeInfo* opcode, uint16_t flags);
  void Remove();
};


}  // namespace hir
}  // namespace alloy


#endif  // ALLOY_HIR_INSTR_H_
