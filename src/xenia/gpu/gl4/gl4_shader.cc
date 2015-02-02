/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/gl4_shader.h"

#include "poly/cxx_compat.h"
#include "poly/math.h"
#include "xenia/gpu/gl4/gl4_gpu-private.h"
#include "xenia/gpu/gl4/gl4_shader_translator.h"
#include "xenia/gpu/gpu-private.h"

namespace xe {
namespace gpu {
namespace gl4 {

using namespace xe::gpu::xenos;

extern "C" GLEWContext* glewGetContext();

// Stateful, but minimally.
thread_local GL4ShaderTranslator shader_translator_;

GL4Shader::GL4Shader(ShaderType shader_type, uint64_t data_hash,
                     const uint32_t* dword_ptr, uint32_t dword_count)
    : Shader(shader_type, data_hash, dword_ptr, dword_count),
      program_(0),
      vao_(0) {}

GL4Shader::~GL4Shader() {
  glDeleteProgram(program_);
  glDeleteVertexArrays(1, &vao_);
}

std::string GL4Shader::GetHeader() {
  static const std::string header =
      "#version 450\n"
      "#extension all : warn\n"
      "#extension GL_ARB_bindless_texture : require\n"
      "#extension GL_ARB_explicit_uniform_location : require\n"
      "#extension GL_ARB_shader_draw_parameters : require\n"
      "#extension GL_ARB_shader_storage_buffer_object : require\n"
      "#extension GL_ARB_shading_language_420pack : require\n"
      "precision highp float;\n"
      "precision highp int;\n"
      "layout(std140, column_major) uniform;\n"
      "layout(std430, column_major) buffer;\n"
      "\n"
      // This must match DrawBatcher::CommonHeader.
      "struct StateData {\n"
      "  vec4 window_offset;\n"
      "  vec4 window_scissor;\n"
      "  vec4 viewport_offset;\n"
      "  vec4 viewport_scale;\n"
      "  vec4 vtx_fmt;\n"
      "  vec4 alpha_test;\n"
      // TODO(benvanik): variable length.
      "  uvec2 texture_samplers[32];\n"
      "  vec4 float_consts[512];\n"
      "  int bool_consts[8];\n"
      "  int loop_consts[32];\n"
      "};\n"
      "layout(binding = 0) buffer State {\n"
      "  StateData states[];\n"
      "};\n"
      "\n"
      "struct VertexData {\n"
      "  vec4 o[16];\n"
      "};\n";
  return header;
}

bool GL4Shader::PrepareVertexArrayObject() {
  glCreateVertexArrays(1, &vao_);

  bool has_bindless_vbos = false;
  if (FLAGS_vendor_gl_extensions && GLEW_NV_vertex_buffer_unified_memory) {
    has_bindless_vbos = true;
    // Nasty, but no DSA for this.
    glBindVertexArray(vao_);
    glEnableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
    glEnableClientState(GL_ELEMENT_ARRAY_UNIFIED_NV);
  }

  uint32_t el_index = 0;
  for (uint32_t buffer_index = 0; buffer_index < buffer_inputs_.count;
       ++buffer_index) {
    const auto& desc = buffer_inputs_.descs[buffer_index];

    for (uint32_t i = 0; i < desc.element_count; ++i, ++el_index) {
      const auto& el = desc.elements[i];
      auto comp_count = GetVertexFormatComponentCount(el.format);
      GLenum comp_type;
      switch (el.format) {
        case VertexFormat::k_8_8_8_8:
          comp_type = el.is_signed ? GL_BYTE : GL_UNSIGNED_BYTE;
          break;
        case VertexFormat::k_2_10_10_10:
          comp_type = el.is_signed ? GL_INT_2_10_10_10_REV
                                   : GL_UNSIGNED_INT_2_10_10_10_REV;
          break;
        case VertexFormat::k_10_11_11:
          // assert_false(el.is_signed);
          XELOGW("Signed k_10_11_11 vertex format not supported by GL");
          comp_type = GL_UNSIGNED_INT_10F_11F_11F_REV;
          break;
        /*case VertexFormat::k_11_11_10:
        break;*/
        case VertexFormat::k_16_16:
          comp_type = el.is_signed ? GL_SHORT : GL_UNSIGNED_SHORT;
          break;
        case VertexFormat::k_16_16_FLOAT:
          comp_type = GL_HALF_FLOAT;
          break;
        case VertexFormat::k_16_16_16_16:
          comp_type = el.is_signed ? GL_SHORT : GL_UNSIGNED_SHORT;
          break;
        case VertexFormat::k_16_16_16_16_FLOAT:
          comp_type = GL_HALF_FLOAT;
          break;
        case VertexFormat::k_32:
          comp_type = el.is_signed ? GL_INT : GL_UNSIGNED_INT;
          break;
        case VertexFormat::k_32_32:
          comp_type = el.is_signed ? GL_INT : GL_UNSIGNED_INT;
          break;
        case VertexFormat::k_32_32_32_32:
          comp_type = el.is_signed ? GL_INT : GL_UNSIGNED_INT;
          break;
        case VertexFormat::k_32_FLOAT:
          comp_type = GL_FLOAT;
          break;
        case VertexFormat::k_32_32_FLOAT:
          comp_type = GL_FLOAT;
          break;
        case VertexFormat::k_32_32_32_FLOAT:
          comp_type = GL_FLOAT;
          break;
        case VertexFormat::k_32_32_32_32_FLOAT:
          comp_type = GL_FLOAT;
          break;
        default:
          assert_unhandled_case(el.format);
          return false;
      }

      glEnableVertexArrayAttrib(vao_, el_index);
      if (has_bindless_vbos) {
        // NOTE: MultiDrawIndirectBindlessMumble doesn't handle separate
        // vertex bindings/formats.
        glVertexAttribFormat(el_index, comp_count, comp_type, el.is_normalized,
                             el.offset_words * 4);
        glVertexArrayVertexBuffer(vao_, el_index, 0, 0, desc.stride_words * 4);
      } else {
        glVertexArrayAttribBinding(vao_, el_index, buffer_index);
        glVertexArrayAttribFormat(vao_, el_index, comp_count, comp_type,
                                  el.is_normalized, el.offset_words * 4);
      }
    }
  }

  if (has_bindless_vbos) {
    glBindVertexArray(0);
  }

  return true;
}

bool GL4Shader::PrepareVertexShader(
    const xenos::xe_gpu_program_cntl_t& program_cntl) {
  if (has_prepared_) {
    return is_valid_;
  }
  has_prepared_ = true;

  // Build static vertex array descriptor.
  if (!PrepareVertexArrayObject()) {
    PLOGE("Unable to prepare vertex shader array object");
    return false;
  }

  std::string apply_transform =
      "vec4 applyTransform(const in StateData state, vec4 pos) {\n"
      "  // Clip->NDC with perspective divide.\n"
      "  // We do this here because it's programmable on the 360.\n"
      "  float w = pos.w;\n"
      "  if (state.vtx_fmt.w == 0.0) {\n"
      "    // w is not 1/W0. Common case.\n"
      "    w = 1.0 / w;\n"
      "  }\n"
      "  if (state.vtx_fmt.x != 0.0) {\n"
      "    // Need to multiply by 1/W0.\n"
      "    pos.xy *= w;\n"
      "  }\n"
      "  if (state.vtx_fmt.z != 0.0) {\n"
      "    // Need to multiply by 1/W0.\n"
      "    pos.z *= w;\n"
      "  }\n"
      "  pos.w = w;\n"
      "  // Perform clipping, lest we get weird geometry.\n"
      // TODO(benvanik): is this right? dxclip mode may change this?
      //"  if (pos.z < gl_DepthRange.near || pos.z > gl_DepthRange.far) {\n"
      //"    // Clipped! w=0 will kill it in the hardware persp divide.\n"
      //"    pos.w = 0.0;\n"
      //"  }\n"
      "  // NDC transform.\n"
      "  pos.x = pos.x * state.viewport_scale.x + \n"
      "      state.viewport_offset.x;\n"
      "  pos.y = pos.y * state.viewport_scale.y + \n"
      "      state.viewport_offset.y;\n"
      "  pos.z = pos.z * state.viewport_scale.z + \n"
      "      state.viewport_offset.z;\n"
      "  // NDC->Window with viewport.\n"
      "  pos.xy = pos.xy * state.window_offset.zw + state.window_offset.xy;\n"
      "  pos.xy = pos.xy / (vec2(1280.0 - 1.0, -720.0 + 1.0) / 2.0) +\n"
      "      vec2(-1.0, 1.0);\n"
      "  // Window adjustment.\n"
      "  return pos;\n"
      "}\n";
  std::string source =
      GetHeader() + apply_transform +
      "out gl_PerVertex {\n"
      "  vec4 gl_Position;\n"
      "  float gl_PointSize;\n"
      "  float gl_ClipDistance[];\n"
      "};\n"
      "layout(location = 0) flat out uint draw_id;\n"
      "layout(location = 1) out VertexData vtx;\n"
      "void processVertex(const in StateData state);\n"
      "void main() {\n" +
      (alloc_counts().positions ? "  gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n"
                                : "") +
      (alloc_counts().point_size ? "  gl_PointSize = 1.0;\n" : "") +
      "  for (int i = 0; i < vtx.o.length(); ++i) {\n"
      "    vtx.o[i] = vec4(0.0, 0.0, 0.0, 0.0);\n"
      "  }\n"
      "  const StateData state = states[gl_DrawIDARB];\n"
      "  processVertex(state);\n"
      "  gl_Position = applyTransform(state, gl_Position);\n"
      "  draw_id = gl_DrawIDARB;\n"
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

  std::string source = GetHeader() +
                       "layout(location = 0) flat in uint draw_id;\n"
                       "layout(location = 1) in VertexData vtx;\n"
                       "layout(location = 0) out vec4 oC[4];\n"
                       "void processFragment(const in StateData state);\n"
                       "void main() {\n" +
                       "  const StateData state = states[draw_id];\n"
                       "  processFragment(state);\n"
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
