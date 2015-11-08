/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/gl/gl_immediate_drawer.h"

#include <string>

#include "xenia/base/assert.h"
#include "xenia/ui/graphics_context.h"

namespace xe {
namespace ui {
namespace gl {

constexpr uint32_t kMaxDrawVertices = 64 * 1024;
constexpr uint32_t kMaxDrawIndices = 64 * 1024;

class GLImmediateTexture : public ImmediateTexture {
 public:
  GLImmediateTexture(uint32_t width, uint32_t height,
                     ImmediateTextureFilter filter, bool repeat)
      : ImmediateTexture(width, height) {
    GLuint gl_handle;
    glCreateTextures(GL_TEXTURE_2D, 1, &gl_handle);

    GLenum gl_filter = GL_NEAREST;
    switch (filter) {
      case ImmediateTextureFilter::kNearest:
        gl_filter = GL_NEAREST;
        break;
      case ImmediateTextureFilter::kLinear:
        gl_filter = GL_LINEAR;
        break;
    }
    glTextureParameteri(gl_handle, GL_TEXTURE_MIN_FILTER, gl_filter);
    glTextureParameteri(gl_handle, GL_TEXTURE_MAG_FILTER, gl_filter);

    glTextureParameteri(gl_handle, GL_TEXTURE_WRAP_S,
                        repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTextureParameteri(gl_handle, GL_TEXTURE_WRAP_T,
                        repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);

    glTextureStorage2D(gl_handle, 1, GL_RGBA8, width, height);

    handle = static_cast<uintptr_t>(gl_handle);
  }

  ~GLImmediateTexture() override {
    GLuint gl_handle = static_cast<GLuint>(handle);
    glDeleteTextures(1, &gl_handle);
  }
};

GLImmediateDrawer::GLImmediateDrawer(GraphicsContext* graphics_context)
    : ImmediateDrawer(graphics_context) {
  glCreateBuffers(1, &vertex_buffer_);
  glNamedBufferStorage(vertex_buffer_,
                       kMaxDrawVertices * sizeof(ImmediateVertex), nullptr,
                       GL_DYNAMIC_STORAGE_BIT);
  glCreateBuffers(1, &index_buffer_);
  glNamedBufferStorage(index_buffer_, kMaxDrawIndices * sizeof(uint16_t),
                       nullptr, GL_DYNAMIC_STORAGE_BIT);

  glCreateVertexArrays(1, &vao_);
  glEnableVertexArrayAttrib(vao_, 0);
  glVertexArrayAttribBinding(vao_, 0, 0);
  glVertexArrayAttribFormat(vao_, 0, 2, GL_FLOAT, GL_FALSE,
                            offsetof(ImmediateVertex, x));
  glEnableVertexArrayAttrib(vao_, 1);
  glVertexArrayAttribBinding(vao_, 1, 0);
  glVertexArrayAttribFormat(vao_, 1, 2, GL_FLOAT, GL_FALSE,
                            offsetof(ImmediateVertex, u));
  glEnableVertexArrayAttrib(vao_, 2);
  glVertexArrayAttribBinding(vao_, 2, 0);
  glVertexArrayAttribFormat(vao_, 2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                            offsetof(ImmediateVertex, color));
  glVertexArrayVertexBuffer(vao_, 0, vertex_buffer_, 0,
                            sizeof(ImmediateVertex));

  InitializeShaders();
}

GLImmediateDrawer::~GLImmediateDrawer() {
  GraphicsContextLock lock(graphics_context_);
  glDeleteBuffers(1, &vertex_buffer_);
  glDeleteBuffers(1, &index_buffer_);
  glDeleteVertexArrays(1, &vao_);
  glDeleteProgram(program_);
}

void GLImmediateDrawer::InitializeShaders() {
  const std::string header =
      R"(
#version 450
#extension GL_ARB_explicit_uniform_location : require
#extension GL_ARB_shading_language_420pack : require
precision highp float;
layout(std140, column_major) uniform;
layout(std430, column_major) buffer;
)";
  const std::string vertex_shader_source = header +
                                           R"(
layout(location = 0) uniform mat4 projection_matrix;
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 0) out vec2 vtx_uv;
layout(location = 1) out vec4 vtx_color;
void main() {
  gl_Position = projection_matrix * vec4(in_pos.xy, 0.0, 1.0);
  vtx_uv = in_uv;
  vtx_color = in_color;
})";
  const std::string fragment_shader_source = header +
                                             R"(
layout(location = 1) uniform sampler2D texture_sampler;
layout(location = 2) uniform int restrict_texture_samples;
layout(location = 0) in vec2 vtx_uv;
layout(location = 1) in vec4 vtx_color;
layout(location = 0) out vec4 out_color;
void main() {
  out_color = vtx_color;
  if (restrict_texture_samples == 0 || vtx_uv.x <= 1.0) {
    vec4 tex_color = texture(texture_sampler, vtx_uv);
    out_color *= tex_color;
    // TODO(benvanik): microprofiler shadows.
  }
})";

  GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  const char* vertex_shader_source_ptr = vertex_shader_source.c_str();
  GLint vertex_shader_source_length = GLint(vertex_shader_source.size());
  glShaderSource(vertex_shader, 1, &vertex_shader_source_ptr,
                 &vertex_shader_source_length);
  glCompileShader(vertex_shader);

  GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  const char* fragment_shader_source_ptr = fragment_shader_source.c_str();
  GLint fragment_shader_source_length = GLint(fragment_shader_source.size());
  glShaderSource(fragment_shader, 1, &fragment_shader_source_ptr,
                 &fragment_shader_source_length);
  glCompileShader(fragment_shader);

  program_ = glCreateProgram();
  glAttachShader(program_, vertex_shader);
  glAttachShader(program_, fragment_shader);
  glLinkProgram(program_);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
}

std::unique_ptr<ImmediateTexture> GLImmediateDrawer::CreateTexture(
    uint32_t width, uint32_t height, ImmediateTextureFilter filter, bool repeat,
    const uint8_t* data) {
  GraphicsContextLock lock(graphics_context_);
  auto texture =
      std::make_unique<GLImmediateTexture>(width, height, filter, repeat);
  if (data) {
    UpdateTexture(texture.get(), data);
  }
  return std::unique_ptr<ImmediateTexture>(texture.release());
}

void GLImmediateDrawer::UpdateTexture(ImmediateTexture* texture,
                                      const uint8_t* data) {
  GraphicsContextLock lock(graphics_context_);
  glTextureSubImage2D(static_cast<GLuint>(texture->handle), 0, 0, 0,
                      texture->width, texture->height, GL_RGBA,
                      GL_UNSIGNED_BYTE, data);
}

void GLImmediateDrawer::Begin(int render_target_width,
                              int render_target_height) {
  was_current_ = graphics_context_->is_current();
  if (!was_current_) {
    graphics_context_->MakeCurrent();
  }

  // Setup render state.
  glEnablei(GL_BLEND, 0);
  glBlendEquationi(0, GL_FUNC_ADD);
  glBlendFunci(0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);

  // Prepare drawing resources.
  glUseProgram(program_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);

  // Setup orthographic projection matrix and viewport.
  const float ortho_projection[4][4] = {
      {2.0f / render_target_width, 0.0f, 0.0f, 0.0f},
      {0.0f, 2.0f / -render_target_height, 0.0f, 0.0f},
      {0.0f, 0.0f, -1.0f, 0.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
  };
  glProgramUniformMatrix4fv(program_, 0, 1, GL_FALSE, &ortho_projection[0][0]);
  glViewport(0, 0, render_target_width, render_target_height);
}

void GLImmediateDrawer::BeginDrawBatch(const ImmediateDrawBatch& batch) {
  assert_true(batch.vertex_count <= kMaxDrawVertices);
  glNamedBufferSubData(vertex_buffer_, 0,
                       batch.vertex_count * sizeof(ImmediateVertex),
                       batch.vertices);
  if (batch.indices) {
    assert_true(batch.index_count <= kMaxDrawIndices);
    glNamedBufferSubData(index_buffer_, 0, batch.index_count * sizeof(uint16_t),
                         batch.indices);
  }

  batch_has_index_buffer_ = !!batch.indices;
}

void GLImmediateDrawer::Draw(const ImmediateDraw& draw) {
  if (draw.scissor) {
    glEnable(GL_SCISSOR_TEST);
    glScissorIndexed(0, draw.scissor_rect[0], draw.scissor_rect[1],
                     draw.scissor_rect[2], draw.scissor_rect[3]);
  } else {
    glDisable(GL_SCISSOR_TEST);
  }

  if (draw.texture_handle) {
    glBindTextureUnit(0, static_cast<GLuint>(draw.texture_handle));
  } else {
    glBindTextureUnit(0, 0);
  }
  glProgramUniform1i(program_, 2, draw.restrict_texture_samples ? 1 : 0);

  GLenum mode = GL_TRIANGLES;
  switch (draw.primitive_type) {
    case ImmediatePrimitiveType::kLines:
      mode = GL_LINES;
      break;
    case ImmediatePrimitiveType::kTriangles:
      mode = GL_TRIANGLES;
      break;
  }

  if (batch_has_index_buffer_) {
    glDrawElementsBaseVertex(
        mode, draw.count, GL_UNSIGNED_SHORT,
        reinterpret_cast<void*>(draw.index_offset * sizeof(uint16_t)),
        draw.base_vertex);
  } else {
    glDrawArrays(mode, draw.base_vertex, draw.count);
  }
}

void GLImmediateDrawer::EndDrawBatch() { glFlush(); }

void GLImmediateDrawer::End() {
  // Restore modified state.
  glDisable(GL_SCISSOR_TEST);
  glBindTextureUnit(0, 0);
  glUseProgram(0);
  glBindVertexArray(0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  if (!was_current_) {
    graphics_context_->ClearCurrent();
  }
}

}  // namespace gl
}  // namespace ui
}  // namespace xe
