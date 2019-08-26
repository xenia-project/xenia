#version 450 core
precision highp float;

layout(location = 0) in vec2 xe_in_texcoord;
layout(location = 1) in vec4 xe_in_color;

layout(location = 0) out vec4 xe_out_color;

layout(push_constant) uniform XePushConstants {
  layout(offset = 8) uint restrict_texture_samples;
} xe_push_constants;

// layout(set = 0, binding = 0) uniform sampler2D xe_immediate_texture_sampler;

void main() {
  xe_out_color = xe_in_color;
  /* if (xe_push_constants.restrict_texture_samples == 0u ||
      xe_in_texcoord.x <= 1.0) {
    xe_out_color *=
        textureLod(xe_immediate_texture_sampler, xe_in_texcoord, 0.0f);
  } */
}
