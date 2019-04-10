#include "xenos_draw.hlsli"

[maxvertexcount(6)]
void main(triangle XeVertex xe_in[3],
          inout TriangleStream<XeVertex> xe_stream) {
  xe_stream.Append(xe_in[0]);
  xe_stream.Append(xe_in[1]);
  xe_stream.Append(xe_in[2]);
  xe_stream.RestartStrip();
  // Find the diagonal (the edge that is longer than both the other two) and
  // mirror the other vertex across it.
  float3 edge_01 = xe_in[1].position.xyz - xe_in[0].position.xyz;
  float3 edge_02 = xe_in[2].position.xyz - xe_in[0].position.xyz;
  float3 edge_12 = xe_in[2].position.xyz - xe_in[1].position.xyz;
  float3 edge_squares = float3(
      dot(edge_01, edge_01), dot(edge_02, edge_02), dot(edge_12, edge_12));
  float3 v3_signs;
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
    xe_stream.Append(xe_in[2]);
    xe_stream.Append(xe_in[1]);
    v3_signs = float3(-1.0f, 1.0f, 1.0f);
  } else if (edge_squares.y > edge_squares.x &&
             edge_squares.y > edge_squares.z) {
    // 02 is the diagonal.
    //
    //  0 ------ 1   0:  -1,-1
    //  | -      |   1:   1,-1
    //  |   \\   |   2:   1, 1
    //  |      - |   3: [-1, 1 ]
    // [3] ----- 2
    xe_stream.Append(xe_in[0]);
    xe_stream.Append(xe_in[2]);
    v3_signs = float3(1.0f, -1.0f, 1.0f);
  } else {
    // 01 is the diagonal. Not seen in any game so far.
    //
    //  0 ------ 2   0:  -1,-1
    //  | -      |   1:   1, 1
    //  |   \\   |   2:   1,-1
    //  |      - |   3: [-1, 1 ]
    // [3] ----- 1
    xe_stream.Append(xe_in[1]);
    xe_stream.Append(xe_in[0]);
    v3_signs = float3(1.0f, 1.0f, -1.0f);
  }
  XeVertex xe_out;
  [unroll] for (int i = 0; i < 16; ++i) {
    xe_out.interpolators[i] = v3_signs.x * xe_in[0].interpolators[i] +
                              v3_signs.y * xe_in[1].interpolators[i] +
                              v3_signs.z * xe_in[2].interpolators[i];
  }
  xe_out.point_params = v3_signs.x * xe_in[0].point_params +
                        v3_signs.y * xe_in[1].point_params +
                        v3_signs.z * xe_in[2].point_params;
  xe_out.clip_space_zw = v3_signs.x * xe_in[0].clip_space_zw +
                         v3_signs.y * xe_in[1].clip_space_zw +
                         v3_signs.z * xe_in[2].clip_space_zw;
  xe_out.position = v3_signs.x * xe_in[0].position +
                    v3_signs.y * xe_in[1].position +
                    v3_signs.z * xe_in[2].position;
  xe_out.clip_distance_0123 = v3_signs.x * xe_in[0].clip_distance_0123 +
                              v3_signs.y * xe_in[1].clip_distance_0123 +
                              v3_signs.z * xe_in[2].clip_distance_0123;
  xe_out.clip_distance_45 = v3_signs.x * xe_in[0].clip_distance_45 +
                            v3_signs.y * xe_in[1].clip_distance_45 +
                            v3_signs.z * xe_in[2].clip_distance_45;
  xe_stream.Append(xe_out);
  xe_stream.RestartStrip();
}
