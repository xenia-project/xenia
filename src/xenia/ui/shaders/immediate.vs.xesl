/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xesl.xesli"

push_const_begin_xe(b0, space0)
  float2_xe xe_coordinate_space_size_inv;
push_const_end_xe

entry_outputs_begin_xe
  entry_out_xe(float2_xe, xe_out_texcoord, 0, TEXCOORD)
  entry_out_xe(float4_xe, xe_out_color, 1, COLOR)
  entry_out_position_xe
entry_outputs_end_stage_inputs_begin_xe
  entry_in_stage_vertex_xe(float2_xe, xe_in_position, 0, POSITION)
  entry_in_stage_vertex_xe(float2_xe, xe_in_texcoord, 1, TEXCOORD)
  entry_in_stage_vertex_xe(float4_xe, xe_in_color, 2, COLOR)
entry_stage_inputs_end_bindings_begin_vertex_xe
  push_const_binding_xe(buffer(0))
entry_bindings_end_inputs_begin_xe
  entry_in_stage_inputs_xe
entry_inputs_end_code_begin_xe
{
  out_xe(xe_out_texcoord) = in_xe(xe_in_texcoord);
  out_xe(xe_out_color) = in_xe(xe_in_color);
  out_position_xe =
      float4_xe(in_xe(xe_in_position) *
                    push_const_xe(xe_coordinate_space_size_inv) *
                    float2_xe(2.0f, 2.0f * NDC_DIRECTION_Y_XE) -
                    float2_xe(1.0f, NDC_DIRECTION_Y_XE),
                0.0f, 1.0f);
}
entry_code_end_xe
