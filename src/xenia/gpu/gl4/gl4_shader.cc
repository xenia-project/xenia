/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/gl4_shader.h"

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/gl4/gl4_gpu_flags.h"
#include "xenia/gpu/gl4/gl4_shader_translator.h"
#include "xenia/gpu/gpu_flags.h"

namespace xe {
namespace gpu {
namespace gl4 {

using namespace xe::gpu::xenos;

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
      "#extension GL_ARB_fragment_coord_conventions : require\n"
      "#define FLT_MAX 3.402823466e+38\n"
      "precision highp float;\n"
      "precision highp int;\n"
      "layout(std140, column_major) uniform;\n"
      "layout(std430, column_major) buffer;\n"
      "\n"
      // This must match DrawBatcher::CommonHeader.
      "struct StateData {\n"
      "  vec4 window_scale;\n"
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

std::string GL4Shader::GetFooter() {
  // http://www.nvidia.com/object/cube_map_ogl_tutorial.html
  // http://developer.amd.com/wordpress/media/2012/10/R600_Instruction_Set_Architecture.pdf
  // src0 = Rn.zzxy, src1 = Rn.yxzz
  // dst.W = FaceId;
  // dst.Z = 2.0f * MajorAxis;
  // dst.Y = S cube coordinate;
  // dst.X = T cube coordinate;
  /*
  major axis
  direction     target                                sc     tc    ma
  ----------   ------------------------------------   ---    ---   ---
  +rx          GL_TEXTURE_CUBE_MAP_POSITIVE_X_EXT=0   -rz    -ry   rx
  -rx          GL_TEXTURE_CUBE_MAP_NEGATIVE_X_EXT=1   +rz    -ry   rx
  +ry          GL_TEXTURE_CUBE_MAP_POSITIVE_Y_EXT=2   +rx    +rz   ry
  -ry          GL_TEXTURE_CUBE_MAP_NEGATIVE_Y_EXT=3   +rx    -rz   ry
  +rz          GL_TEXTURE_CUBE_MAP_POSITIVE_Z_EXT=4   +rx    -ry   rz
  -rz          GL_TEXTURE_CUBE_MAP_NEGATIVE_Z_EXT=5   -rx    -ry   rz
  */
  static const std::string footer =
      "vec4 cube(vec4 src0, vec4 src1) {\n"
      "  vec3 src = vec3(src1.y, src1.x, src1.z);\n"
      "  vec3 abs_src = abs(src);\n"
      "  int face_id;\n"
      "  float sc;\n"
      "  float tc;\n"
      "  float ma;\n"
      "  if (abs_src.x > abs_src.y && abs_src.x > abs_src.z) {\n"
      "    if (src.x > 0.0) {\n"
      "      face_id = 0; sc = -abs_src.z; tc = -abs_src.y; ma = abs_src.x;\n"
      "    } else {\n"
      "      face_id = 1; sc =  abs_src.z; tc = -abs_src.y; ma = abs_src.x;\n"
      "    }\n"
      "  } else if (abs_src.y > abs_src.x && abs_src.y > abs_src.z) {\n"
      "    if (src.y > 0.0) {\n"
      "      face_id = 2; sc =  abs_src.x; tc =  abs_src.z; ma = abs_src.y;\n"
      "    } else {\n"
      "      face_id = 3; sc =  abs_src.x; tc = -abs_src.z; ma = abs_src.y;\n"
      "    }\n"
      "  } else {\n"
      "    if (src.z > 0.0) {\n"
      "      face_id = 4; sc =  abs_src.x; tc = -abs_src.y; ma = abs_src.z;\n"
      "    } else {\n"
      "      face_id = 5; sc = -abs_src.x; tc = -abs_src.y; ma = abs_src.z;\n"
      "    }\n"
      "  }\n"
      "  float s = (sc / ma + 1.0) / 2.0;\n"
      "  float t = (tc / ma + 1.0) / 2.0;\n"
      "  return vec4(t, s, 2.0 * ma, float(face_id));\n"
      "}\n";
  return footer;
}

bool GL4Shader::PrepareVertexArrayObject() {
  glCreateVertexArrays(1, &vao_);

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
      glVertexArrayAttribBinding(vao_, el_index, buffer_index);
      glVertexArrayAttribFormat(vao_, el_index, comp_count, comp_type,
                                el.is_normalized, el.offset_words * 4);
    }
  }

  return true;
}

bool GL4Shader::PrepareVertexShader(
    GL4ShaderTranslator* shader_translator,
    const xenos::xe_gpu_program_cntl_t& program_cntl) {
  if (has_prepared_) {
    return is_valid_;
  }
  has_prepared_ = true;

  // Build static vertex array descriptor.
  if (!PrepareVertexArrayObject()) {
    XELOGE("Unable to prepare vertex shader array object");
    return false;
  }
  std::string apply_transform =
      "vec4 applyTransform(const in StateData state, vec4 pos) {\n"
      "  if (state.vtx_fmt.w == 0.0) {\n"
      "    // w is 1/W0, so fix it.\n"
      "    pos.w = 1.0 / pos.w;\n"
      "  }\n"
      "  if (state.vtx_fmt.x != 0.0) {\n"
      "    // Already multiplied by 1/W0, so pull it out.\n"
      "    pos.xy /= pos.w;\n"
      "  }\n"
      "  if (state.vtx_fmt.z != 0.0) {\n"
      "    // Already multiplied by 1/W0, so pull it out.\n"
      "    pos.z /= pos.w;\n"
      "  }\n"
      "  pos.xy *= state.window_scale.xy;\n"
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
      "}\n" +
      GetFooter();

  std::string translated_source =
      shader_translator->TranslateVertexShader(this, program_cntl);
  if (translated_source.empty()) {
    XELOGE("Vertex shader failed translation");
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
    GL4ShaderTranslator* shader_translator,
    const xenos::xe_gpu_program_cntl_t& program_cntl) {
  if (has_prepared_) {
    return is_valid_;
  }
  has_prepared_ = true;

  std::string source =
      GetHeader() +
      "layout(origin_upper_left, pixel_center_integer) in vec4 gl_FragCoord;\n"
      "layout(location = 0) flat in uint draw_id;\n"
      "layout(location = 1) in VertexData vtx;\n"
      "layout(location = 0) out vec4 oC[4];\n"
      "void processFragment(const in StateData state);\n"
      "void applyAlphaTest(int alpha_func, float alpha_ref) {\n"
      "  bool passes = false;\n"
      "  switch (alpha_func) {\n"
      "  case 0:                                          break;\n"
      "  case 1: if (oC[0].a <  alpha_ref) passes = true; break;\n"
      "  case 2: if (oC[0].a == alpha_ref) passes = true; break;\n"
      "  case 3: if (oC[0].a <= alpha_ref) passes = true; break;\n"
      "  case 4: if (oC[0].a >  alpha_ref) passes = true; break;\n"
      "  case 5: if (oC[0].a != alpha_ref) passes = true; break;\n"
      "  case 6: if (oC[0].a >= alpha_ref) passes = true; break;\n"
      "  case 7:                           passes = true; break;\n"
      "  };\n"
      "  if (!passes) discard;\n"
      "}\n"
      "void main() {\n" +
      "  const StateData state = states[draw_id];\n"
      "  processFragment(state);\n"
      "  if (state.alpha_test.x != 0.0) {\n"
      "    applyAlphaTest(int(state.alpha_test.y), state.alpha_test.z);\n"
      "  }\n"
      "}\n" +
      GetFooter();

  std::string translated_source =
      shader_translator->TranslatePixelShader(this, program_cntl);
  if (translated_source.empty()) {
    XELOGE("Pixel shader failed translation");
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
  char file_name[xe::max_path];
  snprintf(file_name, xe::countof(file_name), "%s/gl4_gen_%.16llX.%s",
           base_path, data_hash_,
           shader_type_ == ShaderType::kVertex ? "vert" : "frag");
  if (!FLAGS_dump_shaders.empty()) {
    // Ensure shader dump path exists.
    auto dump_shaders_path = xe::to_wstring(FLAGS_dump_shaders);
    if (!dump_shaders_path.empty()) {
      dump_shaders_path = xe::to_absolute_path(dump_shaders_path);
      xe::filesystem::CreateFolder(dump_shaders_path);
    }

    // Note that we put the translated source first so we get good line numbers.
    FILE* f = fopen(file_name, "w");
    if (f) {
      fprintf(f, "%s", translated_disassembly_.c_str());
      fprintf(f, "/*\n");
      fprintf(f, "%s", ucode_disassembly_.c_str());
      fprintf(f, " */\n");
      fclose(f);
    }
  }

  program_ = glCreateShaderProgramv(shader_type_ == ShaderType::kVertex
                                        ? GL_VERTEX_SHADER
                                        : GL_FRAGMENT_SHADER,
                                    1, &source_str);
  if (!program_) {
    XELOGE("Unable to create shader program");
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
    XELOGE("Unable to link program: %s", info_log.c_str());
    error_log_ = std::move(info_log);
    assert_always("Unable to link generated shader");
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
      host_disassembly_ = std::string(disasm_start);
    } else {
      host_disassembly_ = std::string("Shader disassembly not available.");
    }

    // Append to shader dump.
    if (!FLAGS_dump_shaders.empty()) {
      if (disasm_start) {
        FILE* f = fopen(file_name, "a");
        fprintf(f, "\n\n/*\n");
        fprintf(f, "%s", disasm_start);
        fprintf(f, "\n*/\n");
        fclose(f);
      } else {
        XELOGW("Got program binary but unable to find disassembly");
      }
    }
  }

  return true;
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
