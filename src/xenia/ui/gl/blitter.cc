/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/gl/blitter.h"

#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/ui/gl/gl_context.h"

namespace xe {
namespace ui {
namespace gl {

Blitter::Blitter()
    : vertex_program_(0),
      color_fragment_program_(0),
      depth_fragment_program_(0),
      color_pipeline_(0),
      depth_pipeline_(0),
      vbo_(0),
      vao_(0),
      nearest_sampler_(0),
      linear_sampler_(0),
      scratch_framebuffer_(0) {}

Blitter::~Blitter() = default;

bool Blitter::Initialize() {
  const std::string header =
      R"(
#version 450
#extension GL_ARB_explicit_uniform_location : require
#extension GL_ARB_shading_language_420pack : require
precision highp float;
precision highp int;
layout(std140, column_major) uniform;
layout(std430, column_major) buffer;
)";
  const std::string vs_source = header +
                                R"(
layout(location = 0) uniform vec4 src_uv;
out gl_PerVertex {
  vec4 gl_Position;
  float gl_PointSize;
  float gl_ClipDistance[];
};
layout(location = 0) in vec2 vfetch_pos;
layout(location = 0) out vec2 vtx_uv;
void main() {
  gl_Position = vec4(vfetch_pos.xy * vec2(2.0, -2.0) -
                vec2(1.0, -1.0), 0.0, 1.0);
  vtx_uv = vfetch_pos.xy * src_uv.zw + src_uv.xy;
})";
  const std::string color_fs_source = header +
                                      R"(
layout(location = 1) uniform sampler2D src_texture;
layout(location = 2) uniform bool swap;
layout(location = 0) in vec2 vtx_uv;
layout(location = 0) out vec4 oC;
void main() {
  oC = texture(src_texture, vtx_uv);
  if (!swap) oC = oC.bgra;
})";
  const std::string depth_fs_source = header +
                                      R"(
layout(location = 1) uniform sampler2D src_texture;
layout(location = 0) in vec2 vtx_uv;
layout(location = 0) out vec4 oC;
void main() {
  gl_FragDepth = texture(src_texture, vtx_uv).r;
})";

  auto vs_source_str = vs_source.c_str();
  vertex_program_ = glCreateShaderProgramv(GL_VERTEX_SHADER, 1, &vs_source_str);
  auto color_fs_source_str = color_fs_source.c_str();
  color_fragment_program_ =
      glCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, &color_fs_source_str);
  auto depth_fs_source_str = depth_fs_source.c_str();
  depth_fragment_program_ =
      glCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, &depth_fs_source_str);
  glCreateProgramPipelines(1, &color_pipeline_);
  glUseProgramStages(color_pipeline_, GL_VERTEX_SHADER_BIT, vertex_program_);
  glUseProgramStages(color_pipeline_, GL_FRAGMENT_SHADER_BIT,
                     color_fragment_program_);
  glCreateProgramPipelines(1, &depth_pipeline_);
  glUseProgramStages(depth_pipeline_, GL_VERTEX_SHADER_BIT, vertex_program_);
  glUseProgramStages(depth_pipeline_, GL_FRAGMENT_SHADER_BIT,
                     depth_fragment_program_);

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

  glCreateFramebuffers(1, &scratch_framebuffer_);

  return true;
}

void Blitter::Shutdown() {
  glDeleteFramebuffers(1, &scratch_framebuffer_);
  glDeleteProgram(vertex_program_);
  glDeleteProgram(color_fragment_program_);
  glDeleteProgram(depth_fragment_program_);
  glDeleteProgramPipelines(1, &color_pipeline_);
  glDeleteProgramPipelines(1, &depth_pipeline_);
  glDeleteBuffers(1, &vbo_);
  glDeleteVertexArrays(1, &vao_);
  glDeleteSamplers(1, &nearest_sampler_);
  glDeleteSamplers(1, &linear_sampler_);
}

struct SavedState {
  GLboolean scissor_test_enabled;
  GLboolean depth_test_enabled;
  GLboolean depth_mask_enabled;
  GLint depth_func;
  GLboolean stencil_test_enabled;
  GLboolean cull_face_enabled;
  GLint cull_face;
  GLint front_face;
  GLint polygon_mode;
  GLboolean color_mask_0_enabled[4];
  GLboolean blend_0_enabled;
  GLint draw_buffer;
  GLfloat viewport[4];
  GLint program_pipeline;
  GLint vertex_array;
  GLint texture_0;
  GLint sampler_0;

  void Save() {
    scissor_test_enabled = glIsEnabled(GL_SCISSOR_TEST);
    depth_test_enabled = glIsEnabled(GL_DEPTH_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask_enabled);
    glGetIntegerv(GL_DEPTH_FUNC, &depth_func);
    stencil_test_enabled = glIsEnabled(GL_STENCIL_TEST);
    cull_face_enabled = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_CULL_FACE_MODE, &cull_face);
    glGetIntegerv(GL_FRONT_FACE, &front_face);
    glGetIntegerv(GL_POLYGON_MODE, &polygon_mode);
    glGetBooleani_v(GL_COLOR_WRITEMASK, 0,
                    reinterpret_cast<GLboolean*>(&color_mask_0_enabled));
    blend_0_enabled = glIsEnabledi(GL_BLEND, 0);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_buffer);
    glGetFloati_v(GL_VIEWPORT, 0, viewport);
    glGetIntegerv(GL_PROGRAM_PIPELINE_BINDING, &program_pipeline);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertex_array);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture_0);
    glGetIntegerv(GL_SAMPLER_BINDING, &sampler_0);
  }

  void Restore() {
    scissor_test_enabled ? glEnable(GL_SCISSOR_TEST)
                         : glDisable(GL_SCISSOR_TEST);
    depth_test_enabled ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    glDepthMask(depth_mask_enabled);
    glDepthFunc(depth_func);
    stencil_test_enabled ? glEnable(GL_STENCIL_TEST)
                         : glDisable(GL_STENCIL_TEST);
    cull_face_enabled ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
    glCullFace(cull_face);
    glFrontFace(front_face);
    glPolygonMode(GL_FRONT_AND_BACK, polygon_mode);
    glColorMaski(0, color_mask_0_enabled[0], color_mask_0_enabled[1],
                 color_mask_0_enabled[2], color_mask_0_enabled[3]);
    blend_0_enabled ? glEnablei(GL_BLEND, 0) : glDisablei(GL_BLEND, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_buffer);
    glViewportIndexedf(0, viewport[0], viewport[1], viewport[2], viewport[3]);
    glBindProgramPipeline(program_pipeline);
    glBindVertexArray(vertex_array);
    glBindTexture(GL_TEXTURE_2D, texture_0);
    glBindSampler(0, sampler_0);
  }
};

void Blitter::Draw(GLuint src_texture, Rect2D src_rect, Rect2D dest_rect,
                   GLenum filter) {
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisablei(GL_BLEND, 0);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CW);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
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

  glViewportIndexedf(0, GLfloat(dest_rect.x), GLfloat(dest_rect.y),
                     GLfloat(dest_rect.width), GLfloat(dest_rect.height));

  // TODO(benvanik): avoid this?
  GLint src_texture_width;
  glGetTextureLevelParameteriv(src_texture, 0, GL_TEXTURE_WIDTH,
                               &src_texture_width);
  GLint src_texture_height;
  glGetTextureLevelParameteriv(src_texture, 0, GL_TEXTURE_HEIGHT,
                               &src_texture_height);
  glProgramUniform4f(vertex_program_, 0,
                     src_rect.x / static_cast<float>(src_texture_width),
                     src_rect.y / static_cast<float>(src_texture_height),
                     src_rect.width / static_cast<float>(src_texture_width),
                     src_rect.height / static_cast<float>(src_texture_height));

  // Useful for seeing the entire framebuffer/etc:
  // glProgramUniform4f(vertex_program_, 0, 0.0f, 0.0f, 1.0f, 1.0f);

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Blitter::BlitTexture2D(GLuint src_texture, Rect2D src_rect,
                            Rect2D dest_rect, GLenum filter,
                            bool swap_channels) {
  SavedState state;
  state.Save();

  glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glStencilMask(0xFF);
  glBindProgramPipeline(color_pipeline_);

  glProgramUniform1i(color_fragment_program_, 2, swap_channels ? 1 : 0);

  Draw(src_texture, src_rect, dest_rect, filter);

  state.Restore();
}

void Blitter::CopyColorTexture2D(GLuint src_texture, Rect2D src_rect,
                                 GLuint dest_texture, Rect2D dest_rect,
                                 GLenum filter, bool swap_channels) {
  SavedState state;
  state.Save();

  glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glBindProgramPipeline(color_pipeline_);

  glProgramUniform1i(color_fragment_program_, 2, swap_channels ? 1 : 0);

  glNamedFramebufferTexture(scratch_framebuffer_, GL_COLOR_ATTACHMENT0,
                            dest_texture, 0);
  glNamedFramebufferDrawBuffer(scratch_framebuffer_, GL_COLOR_ATTACHMENT0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scratch_framebuffer_);
  Draw(src_texture, src_rect, dest_rect, filter);
  glNamedFramebufferDrawBuffer(scratch_framebuffer_, GL_NONE);
  glNamedFramebufferTexture(scratch_framebuffer_, GL_COLOR_ATTACHMENT0, GL_NONE,
                            0);

  state.Restore();
}

void Blitter::CopyDepthTexture(GLuint src_texture, Rect2D src_rect,
                               GLuint dest_texture, Rect2D dest_rect) {
  SavedState state;
  state.Save();

  glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_ALWAYS);
  glDepthMask(GL_TRUE);
  glBindProgramPipeline(depth_pipeline_);

  glNamedFramebufferTexture(scratch_framebuffer_, GL_DEPTH_STENCIL_ATTACHMENT,
                            dest_texture, 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scratch_framebuffer_);
  Draw(src_texture, src_rect, dest_rect, GL_NEAREST);
  glNamedFramebufferTexture(scratch_framebuffer_, GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_NONE, 0);

  state.Restore();
}

}  // namespace gl
}  // namespace ui
}  // namespace xe
