/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_SEQUENCES_H_
#define XENIA_CPU_BACKEND_A64_A64_SEQUENCES_H_

#include "xenia/cpu/hir/instr.h"

#include <unordered_map>

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

class A64Emitter;

typedef bool (*SequenceSelectFn)(A64Emitter&, const hir::Instr*);
extern std::unordered_map<uint32_t, SequenceSelectFn> sequence_table;

template <typename T>
bool Register() {
  sequence_table.insert({T::head_key(), T::Select});
  return true;
}

template <typename T, typename Tn, typename... Ts>
static bool Register() {
  bool b = true;
  b = b && Register<T>();          // Call the above function
  b = b && Register<Tn, Ts...>();  // Call ourself again (recursively)
  return b;
}
#define EMITTER_OPCODE_TABLE(name, ...) \
  const auto A64_INSTR_##name = Register<__VA_ARGS__>();

bool SelectSequence(A64Emitter* e, const hir::Instr* i,
                    const hir::Instr** new_tail);

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_SEQUENCES_H_
