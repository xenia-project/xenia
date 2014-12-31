/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/gl4/gl4_shader.h>

#include <poly/math.h>
#include <xenia/gpu/gl4/gl4_shader_translator.h>
#include <xenia/gpu/gpu-private.h>

namespace xe {
namespace gpu {
namespace gl4 {

extern "C" GLEWContext* glewGetContext();

// Stateful, but minimally.
thread_local GL4ShaderTranslator shader_translator_;

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
    "precision highp float;\n"
    "precision highp int;\n"
    "layout(std140, column_major) uniform;\n"
    "layout(std430, column_major) buffer;\n"
    "struct StateData {\n"
    "  vec4 pretransform;\n"
    "  vec4 window_offset;\n"
    "  vec4 window_scissor;\n"
    "  vec4 viewport_offset;\n"
    "  vec4 viewport_scale;\n"
    "  vec4 alpha_test;\n"
    "  uvec2 texture_samplers[32];\n"
    "  vec4 float_consts[512];\n"
    "  uint fetch_consts[32 * 6];\n"
    "  int bool_consts[8];\n"
    "  int loop_consts[32];\n"
    "};\n"
    "struct VertexData {\n"
    "  vec4 o[16];\n"
    "};\n"
    "\n"
    "layout(binding = 0) buffer State {\n"
    "  StateData state;\n"
    "};\n";

bool GL4Shader::PrepareVertexShader(
    const xenos::xe_gpu_program_cntl_t& program_cntl) {
  if (has_prepared_) {
    return is_valid_;
  }
  has_prepared_ = true;

  std::string apply_viewport =
      "vec4 applyViewport(vec4 pos) {\n"
      "  pos.xy = pos.xy / state.pretransform.zw + state.pretransform.xy;\n"
      "  pos.x = pos.x * state.viewport_scale.x + \n"
      "      state.viewport_offset.x;\n"
      "  pos.y = pos.y * state.viewport_scale.y + \n"
      "      state.viewport_offset.y;\n"
      "  pos.z = pos.z * state.viewport_scale.z + \n"
      "      state.viewport_offset.z;\n"
      "  pos.xy += state.window_offset.xy;\n"
      "  return pos;\n"
      "}\n";
  std::string source =
      header + apply_viewport +
      "out gl_PerVertex {\n"
      "  vec4 gl_Position;\n"
      "  float gl_PointSize;\n"
      "  float gl_ClipDistance[];\n"
      "};\n"
      "layout(location = 0) out VertexData vtx;\n"
      "void processVertex();\n"
      "void main() {\n" +
      (alloc_counts().positions ? "  gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n"
                                : "") +
      (alloc_counts().point_size ? "  gl_PointSize = 1.0;\n" : "") +
      "  for (int i = 0; i < vtx.o.length(); ++i) {\n"
      "    vtx.o[i] = vec4(0.0, 0.0, 0.0, 0.0);\n"
      "  }\n"
      "  processVertex();\n"
      "  gl_Position = applyViewport(gl_Position);\n"
      "}\n";

  std::string translated_source =
      shader_translator_.TranslateVertexShader(this, program_cntl);
  if (translated_source.empty()) {
    PLOGE("Vertex shader failed translation");
    return false;
  }
  source += translated_source;

  if (!CompileProgram(source)) {
    return false;
  }

  is_valid_ = true;
  return true;
}

bool GL4Shader::PreparePixelShader(
    const xenos::xe_gpu_program_cntl_t& program_cntl) {
  if (has_prepared_) {
    return is_valid_;
  }
  has_prepared_ = true;

  std::string source =
      header +
      "layout(location = 0) in VertexData vtx;\n"
      "layout(location = 0) out vec4 oC[4];\n"
      "void processFragment();\n"
      "void main() {\n"
      "  for (int i = 0; i < oC.length(); ++i) {\n"
      "    oC[i] = vec4(1.0, 0.0, 0.0, 1.0);\n"
      "  }\n" +
      (program_cntl.ps_export_depth ? "  gl_FragDepth = 0.0;\n" : "") +
      "  processFragment();\n"
      "}\n";

  std::string translated_source =
      shader_translator_.TranslatePixelShader(this, program_cntl);
  if (translated_source.empty()) {
    PLOGE("Pixel shader failed translation");
    return false;
  }

  source += translated_source;

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

  // Save to disk, if we asked for it.
  auto base_path = FLAGS_dump_shaders.c_str();
  char file_name[poly::max_path];
  snprintf(file_name, poly::countof(file_name), "%s/gl4_gen_%.16llX.%s",
           base_path, data_hash_,
           shader_type_ == ShaderType::kVertex ? "vert" : "frag");
  if (FLAGS_dump_shaders.size()) {
    // Note that we put the translated source first so we get good line numbers.
    FILE* f = fopen(file_name, "w");
    fprintf(f, translated_disassembly_.c_str());
    fprintf(f, "\n\n");
    fprintf(f, "/*\n");
    fprintf(f, ucode_disassembly_.c_str());
    fprintf(f, " */\n");
    fclose(f);
  }

  program_ = glCreateShaderProgramv(shader_type_ == ShaderType::kVertex
                                        ? GL_VERTEX_SHADER
                                        : GL_FRAGMENT_SHADER,
                                    1, &source_str);
  if (!program_) {
    PLOGE("Unable to create shader program");
    return false;
  }

  // Get error log, if we failed to link.
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

  // Get program binary, if it's available.
  GLint binary_length = 0;
  glGetProgramiv(program_, GL_PROGRAM_BINARY_LENGTH, &binary_length);
  if (binary_length) {
    translated_binary_.resize(binary_length);
    GLenum binary_format;
    glGetProgramBinary(program_, binary_length, &binary_length, &binary_format,
                       translated_binary_.data());

    // Append to shader dump.
    if (FLAGS_dump_shaders.size()) {
      // If we are on nvidia, we can find the disassembly string.
      // I haven't been able to figure out from the format how to do this
      // without a search like this.
      const char* disasm_start = nullptr;
      size_t search_offset = 0;
      char* search_start = reinterpret_cast<char*>(translated_binary_.data());
      while (true) {
        auto p = reinterpret_cast<char*>(
            memchr(translated_binary_.data() + search_offset, '!',
                   translated_binary_.size() - search_offset));
        if (!p) {
          break;
        }
        if (p[0] == '!' && p[1] == '!' && p[2] == 'N' && p[3] == 'V') {
          disasm_start = p;
          break;
        }
        search_offset = p - search_start;
        ++search_offset;
      }

      if (disasm_start) {
        FILE* f = fopen(file_name, "a");
        fprintf(f, "\n\n/*\n");
        fprintf(f, disasm_start);
        fprintf(f, "\n*/\n");
        fclose(f);
      } else {
        PLOGW("Got program binary but unable to find disassembly");
      }
    }
  }

  return true;
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
