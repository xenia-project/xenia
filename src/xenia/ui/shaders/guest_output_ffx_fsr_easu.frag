#version 420
#extension GL_GOOGLE_include_directive : require

layout(push_constant) uniform XeFsrEasuConstants {
  // 16 occupied by the vertex shader.
  layout(offset = 16) vec2 xe_fsr_easu_input_output_size_ratio;
  layout(offset = 24) vec2 xe_fsr_easu_input_size_inv;
};

layout(set = 0, binding = 0) uniform sampler2D xe_texture;

layout(location = 0) out vec4 xe_frag_color;

#define A_GPU 1
#define A_GLSL 1
#include "../../../../third_party/FidelityFX-FSR/ffx-fsr/ffx_a.h"
#define FSR_EASU_F 1
vec4 FsrEasuRF(vec2 p) { return textureGather(xe_texture, p, 0); }
vec4 FsrEasuGF(vec2 p) { return textureGather(xe_texture, p, 1); }
vec4 FsrEasuBF(vec2 p) { return textureGather(xe_texture, p, 2); }
#include "../../../../third_party/FidelityFX-FSR/ffx-fsr/ffx_fsr1.h"

void main() {
  // FsrEasuCon with smaller push constant usage.
  uvec4 easu_const_0 =
      uvec4(floatBitsToUint(xe_fsr_easu_input_output_size_ratio),
            floatBitsToUint(0.5 * xe_fsr_easu_input_output_size_ratio - 0.5));
  uvec4 easu_const_1 = floatBitsToUint(vec4(1.0, 1.0, 1.0, -1.0) *
                                       xe_fsr_easu_input_size_inv.xyxy);
  uvec4 easu_const_2 = floatBitsToUint(vec4(-1.0, 2.0, 1.0, 2.0) *
                                       xe_fsr_easu_input_size_inv.xyxy);
  uvec4 easu_const_3 =
      uvec4(floatBitsToUint(0.0),
            floatBitsToUint(4.0 * xe_fsr_easu_input_size_inv.y), 0u, 0u);
  FsrEasuF(xe_frag_color.rgb, uvec2(gl_FragCoord.xy), easu_const_0,
           easu_const_1, easu_const_2, easu_const_3);
  xe_frag_color.a = 1.0;
}
