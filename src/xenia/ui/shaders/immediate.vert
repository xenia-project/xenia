#version 310 es

layout(push_constant) uniform XePushConstants {
  vec2 xe_coordinate_space_size_inv;
};

layout(location = 0) in vec2 xe_attr_position;
layout(location = 1) in vec2 xe_attr_texcoord;
layout(location = 2) in lowp vec4 xe_attr_color;

layout(location = 0) out vec2 xe_var_texcoord;
layout(location = 1) out lowp vec4 xe_var_color;

void main() {
  xe_var_texcoord = xe_attr_texcoord;
  xe_var_color = xe_attr_color;
  gl_Position =
      vec4(xe_attr_position * xe_coordinate_space_size_inv * 2.0 - 1.0, 0.0,
           1.0);
}
