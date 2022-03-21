/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#version 460
#extension GL_GOOGLE_include_directive : require
#include "xenos_gs.glsli"

layout(triangles) in;
layout(triangle_strip, max_vertices=6) out;

void main() {
  if (any(isnan(gl_in[0].gl_Position)) || any(isnan(gl_in[1].gl_Position)) ||
      any(isnan(gl_in[2].gl_Position))) {
    return;
  }

  uint i;

  for (i = 0; i < 3u; ++i) {
    xe_out_interpolators = xe_in_interpolators[i];
    gl_Position = gl_in[i].gl_Position;
    EmitVertex();
  }
  EndPrimitive();

  // Find the diagonal (the edge that is longer than both the other two) and
  // mirror the other vertex across it.
  vec3 edge_01 = gl_in[1].gl_Position.xyz - gl_in[0].gl_Position.xyz;
  vec3 edge_02 = gl_in[2].gl_Position.xyz - gl_in[0].gl_Position.xyz;
  vec3 edge_12 = gl_in[2].gl_Position.xyz - gl_in[1].gl_Position.xyz;
  vec3 edge_squares = vec3(
      dot(edge_01, edge_01), dot(edge_02, edge_02), dot(edge_12, edge_12));
  vec3 v3_signs;
  if (edge_squares.z > edge_squares.x && edge_squares.z > edge_squares.y) {
    // 12 is the diagonal. Most games use this form.
    //
    //  0 ------ 1   0:  -1,-1
    //  |      - |   1:   1,-1
    //  |   //   |   2:  -1, 1
    //  | -      |   3: [ 1, 1 ]
    //  2 ----- [3]
    //
    //  0 ------ 2   0:  -1,-1
    //  |      - |   1:  -1, 1
    //  |   //   |   2:   1,-1
    //  | -      |   3: [ 1, 1 ]
    //  1 ------[3]
    xe_out_interpolators = xe_in_interpolators[2];
    gl_Position = gl_in[2].gl_Position;
    EmitVertex();
    xe_out_interpolators = xe_in_interpolators[1];
    gl_Position = gl_in[1].gl_Position;
    EmitVertex();
    v3_signs = vec3(-1.0, 1.0, 1.0);
  } else if (edge_squares.y > edge_squares.x &&
             edge_squares.y > edge_squares.z) {
    // 02 is the diagonal.
    //
    //  0 ------ 1   0:  -1,-1
    //  | -      |   1:   1,-1
    //  |   \\   |   2:   1, 1
    //  |      - |   3: [-1, 1 ]
    // [3] ----- 2
    xe_out_interpolators = xe_in_interpolators[0];
    gl_Position = gl_in[0].gl_Position;
    EmitVertex();
    xe_out_interpolators = xe_in_interpolators[2];
    gl_Position = gl_in[2].gl_Position;
    EmitVertex();
    v3_signs = vec3(1.0, -1.0, 1.0);
  } else {
    // 01 is the diagonal. Not seen in any game so far.
    //
    //  0 ------ 2   0:  -1,-1
    //  | -      |   1:   1, 1
    //  |   \\   |   2:   1,-1
    //  |      - |   3: [-1, 1 ]
    // [3] ----- 1
    xe_out_interpolators = xe_in_interpolators[1];
    gl_Position = gl_in[1].gl_Position;
    EmitVertex();
    xe_out_interpolators = xe_in_interpolators[0];
    gl_Position = gl_in[0].gl_Position;
    EmitVertex();
    v3_signs = vec3(1.0, 1.0, -1.0);
  }
  for (i = 0; i < 16u; ++i) {
    xe_out_interpolators[i] = v3_signs.x * xe_in_interpolators[0][i] +
                              v3_signs.y * xe_in_interpolators[1][i] +
                              v3_signs.z * xe_in_interpolators[2][i];
  }
  gl_Position = v3_signs.x * gl_in[0].gl_Position +
                v3_signs.y * gl_in[1].gl_Position +
                v3_signs.z * gl_in[2].gl_Position;
  EmitVertex();
  EndPrimitive();
}
