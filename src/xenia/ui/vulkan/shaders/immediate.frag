// NOTE: This file is compiled and embedded into the exe.
//       Use `xenia-build genspirv` and check in any changes under bin/.

#version 450 core
precision highp float;

layout(push_constant) uniform PushConstants {
  layout(offset = 64) int restrict_texture_samples;
} push_constants;

layout(set = 0, binding = 0) uniform sampler2D texture_sampler;

layout(set = 0, binding = 1) uniform sampler1D tex1D;
layout(set = 0, binding = 1) uniform sampler2D tex2D;

layout(location = 0) in vec2 vtx_uv;
layout(location = 1) in vec4 vtx_color;

layout(location = 0) out vec4 out_color;

void main() {
  out_color = vtx_color;
  if (push_constants.restrict_texture_samples == 0 || vtx_uv.x <= 1.0) {
    vec4 tex_color = texture(texture_sampler, vtx_uv);
    out_color *= tex_color;
    // TODO(benvanik): microprofiler shadows.
  }
}
