/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_UCODE_UCODE_OPS_H_
#define XENIA_GPU_UCODE_UCODE_OPS_H_

#include <xenia/core.h>

#include <xenia/gpu/ucode/ucode.h>


namespace xe {
namespace gpu {
namespace ucode {


// This code comes from libxemit by GliGli.


typedef enum {
  XEMR_NONE           = -1,
  XEMR_TEMP           = 0,
  XEMR_CONST          = 1,
  XEMR_COLOR_OUT      = 2,
  XEMR_TEX_FETCH      = 3,
} ucode_reg_type_t;


typedef enum {
  XEMO_NONE           = -1,
  XEMO_SEQUENCER      = 0,
  XEMO_ALU_VECTOR     = 1,
  XEMO_ALU_VECTOR_SAT = 2,
  XEMO_ALU_SCALAR     = 3,
  XEMO_ALU_SCALAR_SAT = 4,
  XEMO_FETCHES        = 5,
} ucode_op_type_t;


typedef struct {
  int8_t            start;
  int8_t            end;
} ucode_mask_t;


typedef struct {
  ucode_reg_type_t  reg_type;
  ucode_mask_t      reg_mask[2]; // there can be 2 reg masks
  uint8_t           swizzle_count;
  ucode_mask_t      swizzle_mask[2]; // there can be 2 swizzle masks
} ucode_reg_t;


typedef struct {
  const char*       name;
  ucode_op_type_t   op_type;
  uint8_t           base_bin[12];
  ucode_mask_t      out_mask;
  ucode_reg_t       regs[4];
} ucode_op_t;


extern const ucode_op_t ucode_ops[];


}  // namespace ucode
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_UCODE_UCODE_OPS_H_
