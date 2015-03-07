/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/blitter.h"

#include <string>

#include "poly/assert.h"
#include "poly/math.h"

namespace xe {
namespace gpu {
namespace gl4 {

extern "C" GLEWContext* glewGetContext();
extern "C" WGLEWContext* wglewGetContext();

Blitter::Blitter()
    : vertex_program_(0),
      fragment_program_(0),
      pipeline_(0),
      vbo_(0),
      vao_(0),
      nearest_sampler_(0),
      linear_sampler_(0) {}

Blitter::~Blitter() = default;

bool Blitter::Initialize() {
  const std::string header =
      "\n\
#version 450 \n\
#extension GL_ARB_explicit_uniform_location : require \n\
#extension GL_ARB_shading_language_420pack : require \n\
precision highp float; \n\
precision highp int; \n\
layout(std140, column_major) uniform; \n\
layout(std430, column_major) buffer; \n\
struct VertexData { \n\
vec2 uv; \n\
}; \n\
";
  const std::string vs_source = header +
                                "\n\
layout(location = 0) uniform vec4 src_uv_params; \n\
out gl_PerVertex { \n\
  vec4 gl_Position; \n\
  float gl_PointSize; \n\
  float gl_ClipDistance[]; \n\
}; \n\
struct VertexFetch { \n\
vec2 pos; \n\
};\n\
layout(location = 0) in VertexFetch vfetch; \n\
layout(location = 0) out VertexData vtx; \n\
void main() { \n\
  gl_Position = vec4(vfetch.pos.xy * vec2(2.0, 2.0) - vec2(1.0, 1.0), 0.0, 1.0); \n\
  vtx.uv = vfetch.pos.xy * src_uv_params.zw + src_uv_params.xy; \n\
} \n\
";
  const std::string fs_source = header +
                                "\n\
layout(location = 1) uniform sampler2D src_texture; \n\
layout(location = 0) in VertexData vtx; \n\
layout(location = 0) out vec4 oC; \n\
void main() { \n\
vec4 color = texture(src_texture, vtx.uv); \n\
oC = color; \n\
} \n\
";

  auto vs_source_str = vs_source.c_str();
  vertex_program_ = glCreateShaderProgramv(GL_VERTEX_SHADER, 1, &vs_source_str);
  auto fs_source_str = fs_source.c_str();
  fragment_program_ =
      glCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, &fs_source_str);
  char log[2048];
  GLsizei log_length;
  glGetProgramInfoLog(vertex_program_, 2048, &log_length, log);
  glCreateProgramPipelines(1, &pipeline_);
  glUseProgramStages(pipeline_, GL_VERTEX_SHADER_BIT, vertex_program_);
  glUseProgramStages(pipeline_, GL_FRAGMENT_SHADER_BIT, fragment_program_);

  glCreateBuffers(1, &vbo_);
  static const GLfloat vbo_data[] = {
      0, 0, 1, 0, 0, 1, 1, 1,
  };
  glNamedBufferStorage(vbo_, sizeof(vbo_data), vbo_data, 0);

  glCreateVertexArrays(1, &vao_);
  glEnableVertexArrayAttrib(vao_, 0);
  glVertexArrayAttribBinding(vao_, 0, 0);
  glVertexArrayAttribFormat(vao_, 0, 2, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(GLfloat) * 2);

  glCreateSamplers(1, &nearest_sampler_);
  glSamplerParameteri(nearest_sampler_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glSamplerParameteri(nearest_sampler_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glSamplerParameteri(nearest_sampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glSamplerParameteri(nearest_sampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glCreateSamplers(1, &linear_sampler_);
  glSamplerParameteri(linear_sampler_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glSamplerParameteri(linear_sampler_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glSamplerParameteri(linear_sampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glSamplerParameteri(linear_sampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  return true;
}

void Blitter::Shutdown() {
  if (vertex_program_) {
    glDeleteProgram(vertex_program_);
  }
  if (fragment_program_) {
    glDeleteProgram(fragment_program_);
  }
  if (pipeline_) {
    glDeleteProgramPipelines(1, &pipeline_);
  }
  if (vbo_) {
    glDeleteBuffers(1, &vbo_);
  }
  if (vao_) {
    glDeleteVertexArrays(1, &vao_);
  }
  if (nearest_sampler_) {
    glDeleteSamplers(1, &nearest_sampler_);
  }
  if (linear_sampler_) {
    glDeleteSamplers(1, &linear_sampler_);
  }
}

void Blitter::Draw(GLuint src_texture, uint32_t src_x, uint32_t src_y,
                   uint32_t src_width, uint32_t src_height, GLenum filter) {
  glDisablei(GL_BLEND, 0);
  glDisable(GL_DEPTH_TEST);
  glBindProgramPipeline(pipeline_);
  glBindVertexArray(vao_);
  glBindTextures(0, 1, &src_texture);
  switch (filter) {
    default:
    case GL_NEAREST:
      glBindSampler(0, nearest_sampler_);
      break;
    case GL_LINEAR:
      glBindSampler(0, linear_sampler_);
      break;
  }

  // TODO(benvanik): avoid this?
  GLint src_texture_width;
  glGetTextureLevelParameteriv(src_texture, 0, GL_TEXTURE_WIDTH,
                               &src_texture_width);
  GLint src_texture_height;
  glGetTextureLevelParameteriv(src_texture, 0, GL_TEXTURE_HEIGHT,
                               &src_texture_height);
  glProgramUniform4f(vertex_program_, 0, src_x / float(src_texture_width),
                     src_y / float(src_texture_height),
                     src_width / float(src_texture_width),
                     src_height / float(src_texture_height));

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  glBindProgramPipeline(0);
  glBindVertexArray(0);
  GLuint zero = 0;
  glBindTextures(0, 1, &zero);
  glBindSampler(0, 0);
}

void Blitter::BlitTexture2D(GLuint src_texture, uint32_t src_x, uint32_t src_y,
                            uint32_t src_width, uint32_t src_height,
                            uint32_t dest_x, uint32_t dest_y,
                            uint32_t dest_width, uint32_t dest_height,
                            GLenum filter) {
  glViewport(dest_x, dest_y, dest_width, dest_height);
  Draw(src_texture, src_x, src_y, src_width, src_height, filter);
}

void Blitter::CopyTexture2D(GLuint src_texture, uint32_t src_x, uint32_t src_y,
                            uint32_t src_width, uint32_t src_height,
                            uint32_t dest_texture, uint32_t dest_x,
                            uint32_t dest_y, uint32_t dest_width,
                            uint32_t dest_height, GLenum filter) {
  glViewport(dest_x, dest_y, dest_width, dest_height);
  //
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
