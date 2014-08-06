/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_COMPILER_PASSES_H_
#define ALLOY_COMPILER_COMPILER_PASSES_H_

#include <alloy/compiler/passes/constant_propagation_pass.h>
#include <alloy/compiler/passes/context_promotion_pass.h>
#include <alloy/compiler/passes/control_flow_analysis_pass.h>
#include <alloy/compiler/passes/control_flow_simplification_pass.h>
#include <alloy/compiler/passes/data_flow_analysis_pass.h>
#include <alloy/compiler/passes/dead_code_elimination_pass.h>
//#include <alloy/compiler/passes/dead_store_elimination_pass.h>
#include <alloy/compiler/passes/finalization_pass.h>
#include <alloy/compiler/passes/register_allocation_pass.h>
#include <alloy/compiler/passes/simplification_pass.h>
#include <alloy/compiler/passes/validation_pass.h>
#include <alloy/compiler/passes/value_reduction_pass.h>

// TODO:
//   - mark_use/mark_set
//     For now: mark_all_changed on all calls
//     For external functions:
//       - load_context/mark_use on all arguments
//       - mark_set on return argument?
//     For internal functions:
//       - if liveness analysis already done, use that
//       - otherwise, assume everything dirty (ACK!)
//       - could use scanner to insert mark_use
//
//   Maybe:
//   - v0.xx = load_constant <c>
//   - v0.xx = load_zero
//   Would prevent NULL defs on values, and make constant de-duping possible.
//   Not sure if it's worth it, though, as the extra register allocation
//   pressure due to de-duped constants seems like it would slow things down
//   a lot.
//
//   - CFG:
//     Blocks need predecessors()/successor()
//     phi Instr reference
//
//   - block liveness tracking (in/out)
//     Block gets:
//       AddIncomingValue(Value* value, Block* src_block) ??

// Potentially interesting passes:
//
// Run order:
//   ContextPromotion
//   Simplification
//   ConstantPropagation
//   TypePropagation
//   ByteSwapElimination
//   Simplification
//   DeadStoreElimination
//   DeadCodeElimination
//
// - TypePropagation
//    There are many extensions/truncations in generated code right now due to
//    various load/stores of varying widths. Being able to find and short-
//    circuit the conversions early on would make following passes cleaner
//    and faster as they'd have to trace through fewer value definitions.
//    Example (after ContextPromotion):
//      v81.i64 = load_context +88
//      v82.i32 = truncate v81.i64
//      v84.i32 = and v82.i32, 3F
//      v85.i64 = zero_extend v84.i32
//      v87.i64 = load_context +248
//      v88.i64 = v85.i64
//      v89.i32 = truncate v88.i64      <-- zero_extend/truncate => v84.i32
//      v90.i32 = byte_swap v89.i32
//      store v87.i64, v90.i32
//    after type propagation / simplification / DCE:
//      v81.i64 = load_context +88
//      v82.i32 = truncate v81.i64
//      v84.i32 = and v82.i32, 3F
//      v87.i64 = load_context +248
//      v90.i32 = byte_swap v84.i32
//      store v87.i64, v90.i32
//
// - ByteSwapElimination
//   Find chained byte swaps and replace with assignments. This is often found
//   in memcpy paths.
//   Example:
//     v0 = load ...
//     v1 = byte_swap v0
//     v2 = byte_swap v1
//     store ..., v2       <-- this could be v0
//
//   It may be tricky to detect, though, as often times there are intervening
//   instructions:
//     v21.i32 = load v20.i64
//     v22.i32 = byte_swap v21.i32
//     v23.i64 = zero_extend v22.i32
//     v88.i64 = v23.i64 (from ContextPromotion)
//     v89.i32 = truncate v88.i64
//     v90.i32 = byte_swap v89.i32
//     store v87.i64, v90.i32
//   After type propagation:
//     v21.i32 = load v20.i64
//     v22.i32 = byte_swap v21.i32
//     v89.i32 = v22.i32
//     v90.i32 = byte_swap v89.i32
//     store v87.i64, v90.i32
//   This could ideally become:
//     v21.i32 = load v20.i64
//     ... (DCE takes care of this) ...
//     store v87.i64, v21.i32
//
// - DeadStoreElimination
//   Generic DSE pass, removing all redundant stores. ContextPromotion may be
//   able to take care of most of these, as the input assembly is generally
//   pretty optimized already. This pass would mainly be looking for introduced
//   stores, such as those from comparisons.
//
//   Example:
//   <block0>:
//     v0 = compare_ult ...     (later removed by DCE)
//     v1 = compare_ugt ...     (later removed by DCE)
//     v2 = compare_eq ...
//     store_context +300, v0   <-- removed
//     store_context +301, v1   <-- removed
//     store_context +302, v2   <-- removed
//     branch_true v1, ...
//   <block1>:
//     v3 = compare_ult ...
//     v4 = compare_ugt ...
//     v5 = compare_eq ...
//     store_context +300, v3   <-- these may be required if at end of function
//     store_context +301, v4       or before a call
//     store_context +302, v5
//     branch_true v5, ...
//
// - X86Canonicalization
//   For various opcodes add copies/commute the arguments to match x86
//   operand semantics. This makes code generation easier and if done
//   before register allocation can prevent a lot of extra shuffling in
//   the emitted code.
//
//   Example:
//   <block0>:
//     v0 = ...
//     v1 = ...
//     v2 = add v0, v1          <-- v1 now unused
//   Becomes:
//     v0 = ...
//     v1 = ...
//     v1 = add v1, v0          <-- src1 = dest/src, so reuse for both
//                                  by commuting and setting dest = src1
//
// - RegisterAllocation
//   Given a machine description (register classes, counts) run over values
//   and assign them to registers, adding spills as needed. It should be
//   possible to directly emit code from this form.
//
//   Example:
//   <block0>:
//     v0 = load_context +0
//     v1 = load_context +1
//     v0 = add v0, v1
//     ...
//     v2 = mul v0, v1
//   Becomes:
//     reg0 = load_context +0
//     reg1 = load_context +1
//     reg2 = add reg0, reg1
//     store_local +123, reg2  <-- spill inserted
//     ...
//     reg0 = load_local +123  <-- load inserted
//     reg0 = mul reg0, reg1

#endif  // ALLOY_COMPILER_COMPILER_PASSES_H_
