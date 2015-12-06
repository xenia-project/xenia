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
#include "xenia/gpu/gpu_flags.h"

namespace xe {
namespace gpu {
namespace gl4 {

GL4Shader::GL4Shader(ShaderType shader_type, uint64_t data_hash,
                     const uint32_t* dword_ptr, uint32_t dword_count)
    : Shader(shader_type, data_hash, dword_ptr, dword_count),
      program_(0),
      vao_(0) {}

GL4Shader::~GL4Shader() {
  glDeleteProgram(program_);
  glDeleteVertexArrays(1, &vao_);
}

bool GL4Shader::Prepare(ShaderTranslator* shader_translator) {
  if (!Shader::Prepare(shader_translator)) {
    return false;
  }

  // Build static vertex array descriptor.
  if (!PrepareVertexArrayObject()) {
    XELOGE("Unable to prepare vertex shader array object");
    return false;
  }

  if (!CompileProgram()) {
    return false;
  }

  return true;
}

bool GL4Shader::PrepareVertexArrayObject() {
  glCreateVertexArrays(1, &vao_);

  for (const auto& vertex_binding : translated_shader_->vertex_bindings()) {
    for (const auto& attrib : vertex_binding.attributes) {
      auto comp_count = GetVertexFormatComponentCount(
          attrib.fetch_instr.attributes.data_format);
      GLenum comp_type;
      bool is_signed = attrib.fetch_instr.attributes.is_signed;
      switch (attrib.fetch_instr.attributes.data_format) {
        case VertexFormat::k_8_8_8_8:
          comp_type = is_signed ? GL_BYTE : GL_UNSIGNED_BYTE;
          break;
        case VertexFormat::k_2_10_10_10:
          comp_type = is_signed ? GL_INT_2_10_10_10_REV
                                : GL_UNSIGNED_INT_2_10_10_10_REV;
          break;
        case VertexFormat::k_10_11_11:
          // assert_false(is_signed);
          XELOGW("Signed k_10_11_11 vertex format not supported by GL");
          comp_type = GL_UNSIGNED_INT_10F_11F_11F_REV;
          break;
        /*case VertexFormat::k_11_11_10:
        break;*/
        case VertexFormat::k_16_16:
          comp_type = is_signed ? GL_SHORT : GL_UNSIGNED_SHORT;
          break;
        case VertexFormat::k_16_16_FLOAT:
          comp_type = GL_HALF_FLOAT;
          break;
        case VertexFormat::k_16_16_16_16:
          comp_type = is_signed ? GL_SHORT : GL_UNSIGNED_SHORT;
          break;
        case VertexFormat::k_16_16_16_16_FLOAT:
          comp_type = GL_HALF_FLOAT;
          break;
        case VertexFormat::k_32:
          comp_type = is_signed ? GL_INT : GL_UNSIGNED_INT;
          break;
        case VertexFormat::k_32_32:
          comp_type = is_signed ? GL_INT : GL_UNSIGNED_INT;
          break;
        case VertexFormat::k_32_32_32_32:
          comp_type = is_signed ? GL_INT : GL_UNSIGNED_INT;
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
          assert_unhandled_case(attrib.fetch_instr.attributes.data_format);
          return false;
      }

      glEnableVertexArrayAttrib(vao_, attrib.attrib_index);
      glVertexArrayAttribBinding(vao_, attrib.attrib_index,
                                 vertex_binding.binding_index);
      glVertexArrayAttribFormat(vao_, attrib.attrib_index, comp_count,
                                comp_type,
                                !attrib.fetch_instr.attributes.is_integer,
                                attrib.fetch_instr.attributes.offset * 4);
    }
  }

  return true;
}

bool GL4Shader::CompileProgram() {
  assert_zero(program_);

  auto source_str = translated_shader_->GetBinaryString();

  // Save to disk, if we asked for it.
  auto base_path = FLAGS_dump_shaders.c_str();
  char file_name[kMaxPath];
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

    char bin_file_name[kMaxPath];
    snprintf(bin_file_name, xe::countof(file_name), "%s/gl4_gen_%.16llX.bin.%s",
             base_path, data_hash_,
             shader_type_ == ShaderType::kVertex ? "vert" : "frag");
    FILE* f = fopen(bin_file_name, "w");
    if (f) {
      fwrite(data_.data(), 4, data_.size(), f);
      fclose(f);
    }

    // Note that we put the translated source first so we get good line numbers.
    f = fopen(file_name, "w");
    if (f) {
      fprintf(f, "%s", source_str.c_str());
      fprintf(f, "/*\n");
      fprintf(f, "%s", translated_shader_->ucode_disassembly().c_str());
      fprintf(f, " */\n");
      fclose(f);
    }
  }

  auto source_str_ptr = source_str.c_str();
  program_ = glCreateShaderProgramv(shader_type_ == ShaderType::kVertex
                                        ? GL_VERTEX_SHADER
                                        : GL_FRAGMENT_SHADER,
                                    1, &source_str_ptr);
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
