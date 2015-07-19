/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/gl/gl4_elemental_renderer.h"

#include <memory>

#include "el/graphics/bitmap_fragment.h"
#include "el/util/math.h"
#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/profiling.h"
#include "xenia/ui/gl/gl_context.h"
#include "xenia/ui/gl/gl.h"

namespace xe {
namespace ui {
namespace gl {

GL4ElementalRenderer::GL4Bitmap::GL4Bitmap(GLContext* context,
                                           GL4ElementalRenderer* renderer)
    : context_(context), renderer_(renderer) {}

GL4ElementalRenderer::GL4Bitmap::~GL4Bitmap() {
  GraphicsContextLock lock(context_);

  // Must flush and unbind before we delete the texture.
  renderer_->FlushBitmap(this);
  glMakeTextureHandleNonResidentARB(gpu_handle_);
  glDeleteTextures(1, &handle_);
}

bool GL4ElementalRenderer::GL4Bitmap::Init(int width, int height,
                                           uint32_t* data) {
  assert(width == el::util::GetNearestPowerOfTwo(width));
  assert(height == el::util::GetNearestPowerOfTwo(height));
  width_ = width;
  height_ = height;

  glCreateTextures(GL_TEXTURE_2D, 1, &handle_);
  glTextureParameteri(handle_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(handle_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(handle_, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTextureParameteri(handle_, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTextureStorage2D(handle_, 1, GL_RGBA8, width_, height_);

  gpu_handle_ = glGetTextureHandleARB(handle_);
  glMakeTextureHandleResidentARB(gpu_handle_);

  set_data(data);

  return true;
}

void GL4ElementalRenderer::GL4Bitmap::set_data(uint32_t* data) {
  renderer_->FlushBitmap(this);
  glTextureSubImage2D(handle_, 0, 0, 0, width_, height_, GL_RGBA,
                      GL_UNSIGNED_BYTE, data);
}

GL4ElementalRenderer::GL4ElementalRenderer(GLContext* context)
    : context_(context),
      vertex_buffer_(max_vertex_batch_size() * sizeof(Vertex)) {}

GL4ElementalRenderer::~GL4ElementalRenderer() {
  GraphicsContextLock lock(context_);
  vertex_buffer_.Shutdown();
  glDeleteVertexArrays(1, &vao_);
  glDeleteProgram(program_);
}

std::unique_ptr<GL4ElementalRenderer> GL4ElementalRenderer::Create(
    GLContext* context) {
  GraphicsContextLock lock(context);
  auto renderer = std::make_unique<GL4ElementalRenderer>(context);
  if (!renderer->Initialize()) {
    XELOGE("Failed to initialize TurboBadger GL4 renderer");
    return nullptr;
  }
  return renderer;
}

bool GL4ElementalRenderer::Initialize() {
  if (!vertex_buffer_.Initialize()) {
    XELOGE("Failed to initialize circular buffer");
    return false;
  }

  const std::string header =
      "\n\
#version 450 \n\
#extension GL_ARB_bindless_texture : require \n\
#extension GL_ARB_explicit_uniform_location : require \n\
#extension GL_ARB_shading_language_420pack : require \n\
precision highp float; \n\
precision highp int; \n\
layout(std140, column_major) uniform; \n\
layout(std430, column_major) buffer; \n\
";
  const std::string vertex_shader_source = header +
                                           "\n\
layout(location = 0) uniform mat4 projection_matrix; \n\
layout(location = 0) in vec2 in_pos; \n\
layout(location = 1) in vec4 in_color; \n\
layout(location = 2) in vec2 in_uv; \n\
layout(location = 0) out vec4 vtx_color; \n\
layout(location = 1) out vec2 vtx_uv; \n\
void main() { \n\
  gl_Position = projection_matrix * vec4(in_pos.xy, 0.0, 1.0); \n\
  vtx_color = in_color; \n\
  vtx_uv = in_uv; \n\
} \n\
";
  const std::string fragment_shader_source = header +
                                             "\n\
layout(location = 1, bindless_sampler) uniform sampler2D texture_sampler; \n\
layout(location = 2) uniform float texture_mix; \n\
layout(location = 0) in vec4 vtx_color; \n\
layout(location = 1) in vec2 vtx_uv; \n\
layout(location = 0) out vec4 oC; \n\
void main() { \n\
  oC = vtx_color; \n\
  if (texture_mix > 0.0) { \n\
    vec4 color = texture(texture_sampler, vtx_uv); \n\
    oC *= color.rgba; \n\
  } \n\
} \n\
";

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

  glCreateVertexArrays(1, &vao_);
  glEnableVertexArrayAttrib(vao_, 0);
  glVertexArrayAttribBinding(vao_, 0, 0);
  glVertexArrayAttribFormat(vao_, 0, 2, GL_FLOAT, GL_FALSE,
                            offsetof(Vertex, x));
  glEnableVertexArrayAttrib(vao_, 1);
  glVertexArrayAttribBinding(vao_, 1, 0);
  glVertexArrayAttribFormat(vao_, 1, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                            offsetof(Vertex, color));
  glEnableVertexArrayAttrib(vao_, 2);
  glVertexArrayAttribBinding(vao_, 2, 0);
  glVertexArrayAttribFormat(vao_, 2, 2, GL_FLOAT, GL_FALSE,
                            offsetof(Vertex, u));
  glVertexArrayVertexBuffer(vao_, 0, vertex_buffer_.handle(), 0,
                            sizeof(Vertex));

  return true;
}

std::unique_ptr<el::graphics::Bitmap> GL4ElementalRenderer::CreateBitmap(
    int width, int height, uint32_t* data) {
  auto bitmap = std::make_unique<GL4Bitmap>(context_, this);
  if (!bitmap->Init(width, height, data)) {
    return nullptr;
  }
  return std::unique_ptr<el::graphics::Bitmap>(bitmap.release());
}

void GL4ElementalRenderer::set_clip_rect(const el::Rect& rect) {
  Flush();
  glScissor(clip_rect_.x, screen_rect_.h - (clip_rect_.y + clip_rect_.h),
            clip_rect_.w, clip_rect_.h);
}

void GL4ElementalRenderer::BeginPaint(int render_target_w,
                                      int render_target_h) {
  Renderer::BeginPaint(render_target_w, render_target_h);

  batch_.vertices = vertices_;

  glEnablei(GL_BLEND, 0);
  glBlendFunci(0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glEnable(GL_SCISSOR_TEST);

  glViewport(0, 0, render_target_w, render_target_h);
  glScissor(0, 0, render_target_w, render_target_h);

  float left = 0.0f;
  float right = float(render_target_w);
  float bottom = float(render_target_h);
  float top = 0.0f;
  float z_near = -1.0f;
  float z_far = 1.0f;
  float projection[16] = {0};
  projection[0] = 2.0f / (right - left);
  projection[5] = 2.0f / (top - bottom);
  projection[10] = -2.0f / (z_far - z_near);
  projection[12] = -(right + left) / (right - left);
  projection[13] = -(top + bottom) / (top - bottom);
  projection[14] = -(z_far + z_near) / (z_far - z_near);
  projection[15] = 1.0f;
  glProgramUniformMatrix4fv(program_, 0, 1, GL_FALSE, projection);

  current_bitmap_ = nullptr;

  glUseProgram(program_);
  glBindVertexArray(vao_);
  glProgramUniform1f(program_, 2, 0.0f);
}

void GL4ElementalRenderer::EndPaint() {
  Renderer::EndPaint();

  Flush();

  glUseProgram(0);
  glBindVertexArray(0);
}

void GL4ElementalRenderer::Flush() {
  if (!draw_command_count_) {
    return;
  }
  vertex_buffer_.Flush();
  for (size_t i = 0; i < draw_command_count_; ++i) {
    glDrawArrays(draw_commands_[i].prim_type,
                 GLint(draw_commands_[i].vertex_offset),
                 GLsizei(draw_commands_[i].vertex_count));
  }
  draw_command_count_ = 0;
  // TODO(benvanik): don't finish here.
  vertex_buffer_.WaitUntilClean();
}

void GL4ElementalRenderer::RenderBatch(Batch* batch) {
  if (draw_command_count_ + 1 > kMaxCommands) {
    Flush();
  }
  size_t total_length = sizeof(Vertex) * batch->vertex_count;
  if (!vertex_buffer_.CanAcquire(total_length)) {
    Flush();
  }

  auto bitmap = static_cast<GL4Bitmap*>(batch->bitmap);
  if (bitmap != current_bitmap_) {
    Flush();
    current_bitmap_ = bitmap;
    glProgramUniformHandleui64ARB(program_, 1,
                                  bitmap ? bitmap->gpu_handle_ : 0);
    glProgramUniform1f(program_, 2, bitmap ? 1.0f : 0.0f);
  }

  auto allocation = vertex_buffer_.Acquire(total_length);

  // TODO(benvanik): custom batcher that lets us use the ringbuffer memory
  // without a copy.
  std::memcpy(allocation.host_ptr, batch->vertices, total_length);

  if (draw_command_count_ &&
      draw_commands_[draw_command_count_ - 1].prim_type == GL_TRIANGLES) {
    // Coalesce.
    assert_always("haven't seen this yet");
    auto& prev_command = draw_commands_[draw_command_count_ - 1];
    prev_command.vertex_count += batch->vertex_count;
  } else {
    auto& command = draw_commands_[draw_command_count_++];
    command.prim_type = GL_TRIANGLES;
    command.vertex_offset = allocation.offset / sizeof(Vertex);
    command.vertex_count = batch->vertex_count;
  }

  vertex_buffer_.Commit(std::move(allocation));
}

}  // namespace gl
}  // namespace ui
}  // namespace xe
