/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/gl4_shader.h"

#include "xenia/base/logging.h"
#include "xenia/base/math.h"

namespace xe {
namespace gpu {
namespace gl4 {

GL4Shader::GL4Shader(ShaderType shader_type, uint64_t data_hash,
                     const uint32_t* dword_ptr, uint32_t dword_count)
    : Shader(shader_type, data_hash, dword_ptr, dword_count) {}

GL4Shader::~GL4Shader() {
  glDeleteProgram(program_);
  glDeleteVertexArrays(1, &vao_);
}

bool GL4Shader::Prepare() {
  // Build static vertex array descriptor.
  if (!PrepareVertexArrayObject()) {
    XELOGE("Unable to prepare vertex shader array object");
    return false;
  }

  bool success = true;
  if (!CompileShader()) {
    host_error_log_ = GetShaderInfoLog();
    success = false;
  }
  if (success && !LinkProgram()) {
    host_error_log_ = GetProgramInfoLog();
    success = false;
  }

  if (success) {
    host_binary_ = GetBinary();
    host_disassembly_ = GetHostDisasmNV(host_binary_);
  }
  is_valid_ = success;

  return success;
}

bool GL4Shader::LoadFromBinary(const uint8_t* blob, GLenum binary_format,
                               size_t length) {
  program_ = glCreateProgram();
  glProgramBinary(program_, binary_format, blob, GLsizei(length));

  GLint link_status = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &link_status);
  if (!link_status) {
    // Failed to link. Not fatal - just clean up so we can get generated later.
    XELOGD("GL4Shader::LoadFromBinary failed. Log:\n%s",
           GetProgramInfoLog().c_str());
    glDeleteProgram(program_);
    program_ = 0;

    return false;
  }

  // Build static vertex array descriptor.
  if (!PrepareVertexArrayObject()) {
    XELOGE("Unable to prepare vertex shader array object");
    return false;
  }

  // Success!
  host_binary_ = GetBinary();
  host_disassembly_ = GetHostDisasmNV(host_binary_);

  is_valid_ = true;
  return true;
}

bool GL4Shader::PrepareVertexArrayObject() {
  glCreateVertexArrays(1, &vao_);

  for (const auto& vertex_binding : vertex_bindings()) {
    for (const auto& attrib : vertex_binding.attributes) {
      auto comp_count = GetVertexFormatComponentCount(
          attrib.fetch_instr.attributes.data_format);
      GLenum comp_type = 0;
      bool is_signed = attrib.fetch_instr.attributes.is_signed;
      switch (attrib.fetch_instr.attributes.data_format) {
        case VertexFormat::k_8_8_8_8:
          comp_type = is_signed ? GL_BYTE : GL_UNSIGNED_BYTE;
          break;
        case VertexFormat::k_2_10_10_10:
          comp_type = is_signed ? GL_INT : GL_UNSIGNED_INT;
          comp_count = 1;
          break;
        case VertexFormat::k_10_11_11:
          comp_type = is_signed ? GL_INT : GL_UNSIGNED_INT;
          comp_count = 1;
          break;
        case VertexFormat::k_11_11_10:
          assert_true(is_signed);
          comp_type = is_signed ? GL_R11F_G11F_B10F : 0;
          break;
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

bool GL4Shader::CompileShader() {
  assert_zero(program_);

  shader_ =
      glCreateShader(shader_type_ == ShaderType::kVertex ? GL_VERTEX_SHADER
                                                         : GL_FRAGMENT_SHADER);
  if (!shader_) {
    XELOGE("OpenGL could not create a shader object!");
    return false;
  }

  auto source_str = GetTranslatedBinaryString();
  auto source_str_ptr = source_str.c_str();
  GLint source_length = GLint(source_str.length());
  glShaderSource(shader_, 1, &source_str_ptr, &source_length);
  glCompileShader(shader_);

  GLint status = 0;
  glGetShaderiv(shader_, GL_COMPILE_STATUS, &status);

  return status == GL_TRUE;
}

bool GL4Shader::LinkProgram() {
  program_ = glCreateProgram();
  if (!program_) {
    XELOGE("OpenGL could not create a shader program!");
    return false;
  }

  glAttachShader(program_, shader_);

  // Enable TFB
  if (shader_type_ == ShaderType::kVertex) {
    const GLchar* feedbackVaryings = "gl_Position";
    glTransformFeedbackVaryings(program_, 1, &feedbackVaryings,
                                GL_SEPARATE_ATTRIBS);
  }

  glProgramParameteri(program_, GL_PROGRAM_SEPARABLE, GL_TRUE);
  glLinkProgram(program_);

  GLint link_status = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &link_status);
  if (!link_status) {
    assert_always("Unable to link generated shader");
    return false;
  }

  return true;
}

std::string GL4Shader::GetShaderInfoLog() {
  if (!shader_) {
    return "GL4Shader::GetShaderInfoLog(): Program is NULL";
  }

  std::string log;
  GLint log_length = 0;
  glGetShaderiv(shader_, GL_INFO_LOG_LENGTH, &log_length);
  if (log_length > 0) {
    log.resize(log_length - 1);
    glGetShaderInfoLog(shader_, log_length, &log_length, &log[0]);
  }

  return log;
}

std::string GL4Shader::GetProgramInfoLog() {
  if (!program_) {
    return "GL4Shader::GetProgramInfoLog(): Program is NULL";
  }

  std::string log;
  GLint log_length = 0;
  glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &log_length);
  if (log_length > 0) {
    log.resize(log_length - 1);
    glGetProgramInfoLog(program_, log_length, &log_length, &log[0]);
  }

  return log;
}

std::vector<uint8_t> GL4Shader::GetBinary(GLenum* binary_format) {
  std::vector<uint8_t> binary;

  // Get program binary, if it's available.
  GLint binary_length = 0;
  glGetProgramiv(program_, GL_PROGRAM_BINARY_LENGTH, &binary_length);
  if (binary_length) {
    binary.resize(binary_length);
    GLenum binary_format_tmp = 0;
    glGetProgramBinary(program_, binary_length, &binary_length,
                       &binary_format_tmp, binary.data());

    if (binary_format) {
      *binary_format = binary_format_tmp;
    }
  }

  return binary;
}

std::string GL4Shader::GetHostDisasmNV(const std::vector<uint8_t>& binary) {
  // If we are on nvidia, we can find the disassembly string.
  // I haven't been able to figure out from the format how to do this
  // without a search like this.
  std::string disasm;

  const char* disasm_start = nullptr;
  size_t search_offset = 0;
  const char* search_start = reinterpret_cast<const char*>(binary.data());
  while (true) {
    auto p = reinterpret_cast<const char*>(memchr(
        binary.data() + search_offset, '!', binary.size() - search_offset));
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
    disasm = std::string(disasm_start);
  } else {
    disasm = std::string("Shader disassembly not available.");
  }

  return disasm;
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
