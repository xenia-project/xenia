/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_COMPILER_PASSES_H_
#define XENIA_CPU_COMPILER_COMPILER_PASSES_H_

#include "xenia/cpu/compiler/passes/conditional_group_pass.h"
#include "xenia/cpu/compiler/passes/conditional_group_subpass.h"
#include "xenia/cpu/compiler/passes/constant_propagation_pass.h"
#include "xenia/cpu/compiler/passes/context_promotion_pass.h"
#include "xenia/cpu/compiler/passes/control_flow_analysis_pass.h"
#include "xenia/cpu/compiler/passes/control_flow_simplification_pass.h"
#include "xenia/cpu/compiler/passes/data_flow_analysis_pass.h"
#include "xenia/cpu/compiler/passes/dead_code_elimination_pass.h"
#include "xenia/cpu/compiler/passes/finalization_pass.h"
#include "xenia/cpu/compiler/passes/memory_sequence_combination_pass.h"
#include "xenia/cpu/compiler/passes/register_allocation_pass.h"
#include "xenia/cpu/compiler/passes/simplification_pass.h"
#include "xenia/cpu/compiler/passes/validation_pass.h"
#include "xenia/cpu/compiler/passes/value_reduction_pass.h"

#endif  // XENIA_CPU_COMPILER_COMPILER_PASSES_H_
