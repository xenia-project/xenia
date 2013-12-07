/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_PASSES_H_
#define ALLOY_COMPILER_PASSES_H_

//#include <alloy/compiler/passes/constant_propagation_pass.h>
//#include <alloy/compiler/passes/context_promotion_pass.h>
#include <alloy/compiler/passes/dead_code_elimination_pass.h>
//#include <alloy/compiler/passes/dead_store_elimination_pass.h>
#include <alloy/compiler/passes/simplification_pass.h>

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
//   ConstantPropagation
//   TypePropagation
//   ByteSwapElimination
//   Simplification
//   DeadStoreElimination
//   DeadCodeElimination
//
// - ContextPromotion
//   Like mem2reg, but because context memory is unaliasable it's easier to
//   check and convert LoadContext/StoreContext into value operations.
//   Example of load->value promotion:
//     v0 = load_context +100
//     store_context +200, v0
//     v1 = load_context +100  <-- replace with v1 = v0
//     store_context +200, v1
//
//   It'd be possible in this stage to also remove redundant context stores:
//   Example of dead store elimination:
//     store_context +100, v0  <-- removed due to following store
//     store_context +100, v1
//   This is more generally done by DSE, however if it could be done here
//   instead as it may be faster (at least on the block-level).
//
// - ConstantPropagation
//   One ContextPromotion has run there will likely be a whole slew of
//   constants that can be pushed through the function.
//   Example:
//     store_context +100, 1000
//     v0 = load_context +100
//     v1 = add v0, v0
//     store_context +200, v1
//   after PromoteContext:
//     store_context +100, 1000
//     v0 = 1000
//     v1 = add v0, v0
//     store_context +200, v1
//   after PropagateConstants:
//     store_context +100, 1000
//     v0 = 1000
//     v1 = add 1000, 1000
//     store_context +200, 2000
//   A DCE run after this should clean up any of the values no longer needed.
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
// - Simplification
//   Run over the instructions and rename assigned variables:
//     v1 = v0
//     v2 = v1
//     v3 = add v0, v2
//   becomes:
//     v1 = v0  (will be removed by DCE)
//     v2 = v0  (will be removed by DCE)
//     v3 = add v0, v0
//   This could be run several times, as it could make other passes faster
//   to compute (for example, ConstantPropagation). DCE will take care of
//   the useless assigns.
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
// - DeadCodeElimination
//   ContextPromotion/DSE will likely leave around a lot of dead statements.
//   Code generated for comparison/testing produces many unused statements and
//   with proper use analysis it should be possible to remove most of them:
//   After context promotion/simplification:
//     v33.i8 = compare_ult v31.i32, 0
//     v34.i8 = compare_ugt v31.i32, 0
//     v35.i8 = compare_eq v31.i32, 0
//     store_context +300, v33.i8
//     store_context +301, v34.i8
//     store_context +302, v35.i8
//     branch_true v35.i8, loc_8201A484
//   After DSE:
//     v33.i8 = compare_ult v31.i32, 0
//     v34.i8 = compare_ugt v31.i32, 0
//     v35.i8 = compare_eq v31.i32, 0
//     branch_true v35.i8, loc_8201A484
//   After DCE:
//     v35.i8 = compare_eq v31.i32, 0
//     branch_true v35.i8, loc_8201A484
//

#endif  // ALLOY_COMPILER_PASSES_H_
