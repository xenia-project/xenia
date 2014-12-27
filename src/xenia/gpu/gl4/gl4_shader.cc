/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/gl4/gl4_shader.h>

#include <xenia/gpu/gpu-private.h>

namespace xe {
namespace gpu {
namespace gl4 {

extern "C" GLEWContext* glewGetContext();

GL4Shader::GL4Shader(ShaderType shader_type, uint64_t data_hash,
                     const uint32_t* dword_ptr, uint32_t dword_count)
    : Shader(shader_type, data_hash, dword_ptr, dword_count), program_(0) {}

GL4Shader::~GL4Shader() { glDeleteProgram(program_); }

const std::string header =
    "#version 450\n"
    "#extension all : warn\n"
    "#extension GL_ARB_bindless_texture : require\n"
    "#extension GL_ARB_explicit_uniform_location : require\n"
    "#extension GL_ARB_shading_language_420pack : require\n"
    "#extension GL_ARB_shader_storage_buffer_object : require\n"
    "#extension GL_NV_shader_buffer_load : require\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "layout(std140, column_major) uniform;\n"
    "layout(std430, column_major) buffer;\n"
    "struct StateData {\n"
    "  vec4 window_offset;\n"
    "  vec4 window_scissor;\n"
    "  vec4 viewport_offset;\n"
    "  vec4 viewport_scale;\n"
    "  vec4 alpha_test;\n"
    "  vec4 float_consts[512];\n"
    "  uint fetch_consts[32 * 6];\n"
    "  int bool_consts[8];\n"
    "  int loop_consts[32];\n"
    "};\n"
    "struct VertexData {\n"
    "  vec4 o[16];\n"
    "};\n"
    "\n"
    "uniform StateData* state;\n";

bool GL4Shader::PrepareVertexShader(
    const xenos::xe_gpu_program_cntl_t& program_cntl) {
  if (has_prepared_) {
    return is_valid_;
  }
  has_prepared_ = true;

  std::string apply_viewport =
      "vec4 applyViewport(vec4 pos) {\n"
      // TODO(benvanik): piecewise viewport_enable -> offset/scale logic.
      "  if (false) {\n"
      "  } else {\n"
      /*"    pos.xy = pos.xy / vec2(state->window_offset.z / 2.0, "
      "-state->window_offset.w / 2.0) + vec2(-1.0, 1.0);\n"
      "    pos.zw = vec2(0.0, 1.0);\n"*/
      "    pos.xy = pos.xy / vec2(1280.0 / 2.0, "
      "-720.0 / 2.0) + vec2(-1.0, 1.0);\n"
      "    //pos.zw = vec2(0.0, 1.0);\n"
      "  }\n"
      "  pos.x = pos.x * state->viewport_scale.x + \n"
      "      state->viewport_offset.x;\n"
      "  pos.y = pos.y * state->viewport_scale.y + \n"
      "      state->viewport_offset.y;\n"
      "  pos.z = pos.z * state->viewport_scale.z + \n"
      "      state->viewport_offset.z;\n"
      "  pos.xy += state->window_offset.xy;\n"
      "  return pos;\n"
      "}\n";
  std::string source =
      header + apply_viewport +
      "out gl_PerVertex {\n"
      "  vec4 gl_Position;\n"
      "  float gl_PointSize;\n"
      "  float gl_ClipDistance[];\n"
      "};\n"
      "layout(location = 0) in vec3 iF0;\n"
      "layout(location = 1) in vec4 iF1;\n"
      "layout(location = 0) out VertexData vtx;\n"
      "void main() {\n"
      //"  vec4 oPos = vec4(iF0.xy, 0.0, 1.0);\n"
      "  vec4 oPos = iF0.xxxx * state->float_consts[0];\n"
      "  oPos = (iF0.yyyy * state->float_consts[1]) + oPos;\n"
      "  oPos = (iF0.zzzz * state->float_consts[2]) + oPos;\n"
      "  oPos = (vec4(1.0, 1.0, 1.0, 1.0) * state->float_consts[3]) + oPos;\n"
      //"  gl_PointSize = 1.0;\n"
      "  for (int i = 0; i < vtx.o.length(); ++i) {\n"
      "    vtx.o[0] = vec4(0.0, 0.0, 0.0, 0.0);\n"
      "  }\n"
      "  vtx.o[0] = iF1;\n"
      "  gl_Position = applyViewport(oPos);\n"
      //"  gl_Position = oPos;\n"
      "}\n";

  if (!CompileProgram(source)) {
    return false;
  }

  is_valid_ = true;
  return true;
}

bool GL4Shader::PreparePixelShader(
    const xenos::xe_gpu_program_cntl_t& program_cntl,
    GL4Shader* vertex_shader) {
  if (has_prepared_) {
    return is_valid_;
  }
  has_prepared_ = true;

  std::string source = header +
                       "layout(location = 0) in VertexData vtx;\n"
                       "layout(location = 0) out vec4 oC[4];\n"
                       "void main() {\n"
                       "  for (int i = 0; i < oC.length(); ++i) {\n"
                       "    oC[i] = vec4(1.0, 0.0, 0.0, 1.0);\n"
                       "  }\n"
                       "  oC[0] = vtx.o[0];\n"
                       //"  gl_FragDepth = 0.0;\n"
                       "}\n";

  if (!CompileProgram(source)) {
    return false;
  }

  is_valid_ = true;
  return true;
}

bool GL4Shader::CompileProgram(std::string source) {
  assert_zero(program_);

  translated_disassembly_ = std::move(source);
  const char* source_str = translated_disassembly_.c_str();

  program_ = glCreateShaderProgramv(shader_type_ == ShaderType::kVertex
                                        ? GL_VERTEX_SHADER
                                        : GL_FRAGMENT_SHADER,
                                    1, &source_str);
  if (!program_) {
    PLOGE("Unable to create shader program");
    return false;
  }

  GLint link_status = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &link_status);
  if (!link_status) {
    // log_length includes the null character.
    GLint log_length = 0;
    glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &log_length);
    std::string info_log;
    info_log.resize(log_length - 1);
    glGetProgramInfoLog(program_, log_length, &log_length,
                        const_cast<char*>(info_log.data()));
    PLOGE("Unable to link program: %s", info_log.c_str());
    error_log_ = std::move(info_log);
    return false;
  }

  return true;
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
