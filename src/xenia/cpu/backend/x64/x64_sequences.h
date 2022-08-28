/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_X64_X64_SEQUENCES_H_
#define XENIA_CPU_BACKEND_X64_X64_SEQUENCES_H_

#include "xenia/base/logging.h"
#include "xenia/cpu/hir/instr.h"

#include <unordered_map>
#define assert_impossible_sequence(name)          \
  assert_always("impossible sequence hit" #name); \
  XELOGE("impossible sequence hit: {}", #name)

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

class X64Emitter;

typedef bool (*SequenceSelectFn)(X64Emitter&, const hir::Instr*, uint32_t ikey);
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
  const auto X64_INSTR_##name = Register<__VA_ARGS__>();

bool SelectSequence(X64Emitter* e, const hir::Instr* i,
                    const hir::Instr** new_tail);

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_X64_X64_SEQUENCES_H_
