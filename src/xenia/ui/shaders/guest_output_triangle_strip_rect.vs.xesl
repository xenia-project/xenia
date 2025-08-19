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
  // If the layout is changed, update the base offset in all guest output pixel
  // shaders!
  float2_xe xe_triangle_strip_rect_offset;
  // Can be negative.
  float2_xe xe_triangle_strip_rect_size;
push_const_end_xe

entry_outputs_begin_xe
  entry_out_position_xe
entry_outputs_end_stage_inputs_begin_xe
entry_stage_inputs_end_bindings_begin_vertex_xe
  push_const_binding_xe(buffer(0))
entry_bindings_end_inputs_begin_xe
  entry_in_vertex_id_xe
entry_inputs_end_code_begin_xe
{
  out_position_xe =
      float4_xe(
          push_const_xe(xe_triangle_strip_rect_offset) +
              float2_xe((uint_x2_xe(in_vertex_id_xe) >>
                         uint2_xe(0u, 1u)) & 1u) *
              push_const_xe(xe_triangle_strip_rect_size),
          0.0f, 1.0f);
}
entry_code_end_xe
