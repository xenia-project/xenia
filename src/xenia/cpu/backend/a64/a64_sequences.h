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
#include <functional>
#include <iostream> // For logging

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

class A64Emitter;

typedef bool (*SequenceSelectFn)(A64Emitter&, const hir::Instr*);

// Singleton accessor for sequence_table
inline std::unordered_map<uint32_t, SequenceSelectFn>& GetSequenceTable() {
    static std::unordered_map<uint32_t, SequenceSelectFn> sequence_table;
    return sequence_table;
}

// Registration Functions
template <typename T>
bool RegisterSingle() {
    bool inserted = GetSequenceTable().emplace(T::head_key(), T::Select).second;
    if (!inserted) {
        std::cerr << "Warning: Duplicate head_key detected for key " << T::head_key() << std::endl;
    }
    return inserted;
}

template <typename... Ts>
bool RegisterAll() {
    return (RegisterSingle<Ts>() && ...); // Fold expression (C++17)
}

// Macro for Registration
#define EMITTER_OPCODE_TABLE(name, ...) \
    static const bool A64_INSTR_##name = RegisterAll<__VA_ARGS__>();

// Function to Select Sequence
bool SelectSequence(A64Emitter* e, const hir::Instr* i,
                    const hir::Instr** new_tail);

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_SEQUENCES_H_
