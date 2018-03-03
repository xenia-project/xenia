/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/gl4_command_processor.h"

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gl4/gl4_gpu_flags.h"
#include "xenia/gpu/gl4/gl4_graphics_system.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/sampler_info.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/xenos.h"

#include "third_party/xxhash/xxhash.h"

DEFINE_bool(draw_all_framebuffers, false,
            "Copy all render targets to screen on swap");

namespace xe {
namespace gpu {
namespace gl4 {

using namespace xe::gpu::xenos;

const GLuint kAnyTarget = UINT_MAX;

// All uncached vertex/index data goes here. If it fills up we need to sync
// with the GPU, so this should be large enough to prevent that in a normal
// frame.
const size_t kScratchBufferCapacity = 256 * 1024 * 1024;
const size_t kScratchBufferAlignment = 256;

GL4CommandProcessor::CachedPipeline::CachedPipeline()
    : vertex_program(0), fragment_program(0), handles({0}) {}

GL4CommandProcessor::CachedPipeline::~CachedPipeline() {
  glDeleteProgramPipelines(1, &handles.default_pipeline);
  glDeleteProgramPipelines(1, &handles.point_list_pipeline);
  glDeleteProgramPipelines(1, &handles.rect_list_pipeline);
  glDeleteProgramPipelines(1, &handles.quad_list_pipeline);
  glDeleteProgramPipelines(1, &handles.line_quad_list_pipeline);
}

GL4CommandProcessor::GL4CommandProcessor(GL4GraphicsSystem* graphics_system,
                                         kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state),
      shader_translator_(GlslShaderTranslator::Dialect::kGL45),
      draw_batcher_(graphics_system_->register_file()),
      scratch_buffer_(kScratchBufferCapacity, kScratchBufferAlignment),
      shader_cache_(&shader_translator_) {}

GL4CommandProcessor::~GL4CommandProcessor() = default;

void GL4CommandProcessor::ClearCaches() {
  texture_cache()->Clear();

  for (auto& cached_framebuffer : cached_framebuffers_) {
    glDeleteFramebuffers(1, &cached_framebuffer.framebuffer);
  }
  cached_framebuffers_.clear();

  for (auto& cached_color_render_target : cached_color_render_targets_) {
    glDeleteTextures(1, &cached_color_render_target.texture);
  }
  cached_color_render_targets_.clear();

  for (auto& cached_depth_render_target : cached_depth_render_targets_) {
    glDeleteTextures(1, &cached_depth_render_target.texture);
  }
  cached_depth_render_targets_.clear();

  CommandProcessor::ClearCaches();
}

bool GL4CommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) {
    XELOGE("Unable to initialize base command processor context");
    return false;
  }

  // Circular buffer holding scratch vertex/index data.
  if (!scratch_buffer_.Initialize()) {
    XELOGE("Unable to initialize scratch buffer");
    return false;
  }

  // Command buffer.
  if (!draw_batcher_.Initialize(&scratch_buffer_)) {
    XELOGE("Unable to initialize command buffer");
    return false;
  }

  // Texture cache that keeps track of any textures/samplers used.
  if (!texture_cache_.Initialize(memory_, &scratch_buffer_)) {
    XELOGE("Unable to initialize texture cache");
    return false;
  }

  const std::string geometry_header =
      "#version 450\n"
      "#extension all : warn\n"
      "#extension GL_ARB_explicit_uniform_location : require\n"
      "#extension GL_ARB_shading_language_420pack : require\n"
      "in gl_PerVertex {\n"
      "  vec4 gl_Position;\n"
      "  float gl_PointSize;\n"
      "  float gl_ClipDistance[];\n"
      "} gl_in[];\n"
      "out gl_PerVertex {\n"
      "  vec4 gl_Position;\n"
      "  float gl_PointSize;\n"
      "  float gl_ClipDistance[];\n"
      "};\n"
      "struct VertexData {\n"
      "  vec4 o[16];\n"
      "};\n"
      "\n"
      "layout(location = 1) in VertexData in_vtx[];\n"
      "layout(location = 1) out VertexData out_vtx;\n";
  // TODO(benvanik): fetch default point size from register and use that if
  //     the VS doesn't write oPointSize.
  // TODO(benvanik): clamp to min/max.
  // TODO(benvanik): figure out how to see which interpolator gets adjusted.
  std::string point_list_shader =
      geometry_header +
      "layout(points) in;\n"
      "layout(triangle_strip, max_vertices = 4) out;\n"
      "void main() {\n"
      "  const vec2 offsets[4] = {\n"
      "    vec2(-1.0,  1.0),\n"
      "    vec2( 1.0,  1.0),\n"
      "    vec2(-1.0, -1.0),\n"
      "    vec2( 1.0, -1.0),\n"
      "  };\n"
      "  vec4 pos = gl_in[0].gl_Position;\n"
      "  float psize = gl_in[0].gl_PointSize;\n"
      "  for (int i = 0; i < 4; ++i) {\n"
      "    gl_Position = vec4(pos.xy + offsets[i] * psize, pos.zw);\n"
      "    out_vtx = in_vtx[0];\n"
      "    EmitVertex();\n"
      "  }\n"
      "  EndPrimitive();\n"
      "}\n";
  std::string rect_list_shader =
      geometry_header +
      "layout(triangles) in;\n"
      "layout(triangle_strip, max_vertices = 6) out;\n"
      "void main() {\n"
      // Most games use the left-aligned form.
      "  bool left_aligned = gl_in[0].gl_Position.x == \n"
      "     gl_in[2].gl_Position.x;\n"
      "  if (left_aligned) {\n"
      //  0 ------ 1
      //  |      - |
      //  |   //   |
      //  | -      |
      //  2 ----- [3]
      "    gl_Position = gl_in[0].gl_Position;\n"
      "    gl_PointSize = gl_in[0].gl_PointSize;\n"
      "    out_vtx = in_vtx[0];\n"
      "    EmitVertex();\n"
      "    gl_Position = gl_in[1].gl_Position;\n"
      "    gl_PointSize = gl_in[1].gl_PointSize;\n"
      "    out_vtx = in_vtx[1];\n"
      "    EmitVertex();\n"
      "    gl_Position = gl_in[2].gl_Position;\n"
      "    gl_PointSize = gl_in[2].gl_PointSize;\n"
      "    out_vtx = in_vtx[2];\n"
      "    EmitVertex();\n"
      "    EndPrimitive();\n"
      "    gl_Position = gl_in[2].gl_Position;\n"
      "    gl_PointSize = gl_in[2].gl_PointSize;\n"
      "    out_vtx = in_vtx[2];\n"
      "    EmitVertex();\n"
      "    gl_Position = gl_in[1].gl_Position;\n"
      "    gl_PointSize = gl_in[1].gl_PointSize;\n"
      "    out_vtx = in_vtx[1];\n"
      "    EmitVertex();\n"
      "    gl_Position = \n"
      "       (gl_in[1].gl_Position + gl_in[2].gl_Position) - \n"
      "       gl_in[0].gl_Position;\n"
      "    gl_PointSize = gl_in[2].gl_PointSize;\n"
      "    for (int i = 0; i < 16; ++i) {\n"
      "      out_vtx.o[i] = -in_vtx[0].o[i] + in_vtx[1].o[i] + \n"
      "          in_vtx[2].o[i];\n"
      "    }\n"
      "    EmitVertex();\n"
      "    EndPrimitive();\n"
      "  } else {\n"
      //  0 ------ 1
      //  | -      |
      //  |   \\   |
      //  |      - |
      // [3] ----- 2
      "    gl_Position = gl_in[0].gl_Position;\n"
      "    gl_PointSize = gl_in[0].gl_PointSize;\n"
      "    out_vtx = in_vtx[0];\n"
      "    EmitVertex();\n"
      "    gl_Position = gl_in[1].gl_Position;\n"
      "    gl_PointSize = gl_in[1].gl_PointSize;\n"
      "    out_vtx = in_vtx[1];\n"
      "    EmitVertex();\n"
      "    gl_Position = gl_in[2].gl_Position;\n"
      "    gl_PointSize = gl_in[2].gl_PointSize;\n"
      "    out_vtx = in_vtx[2];\n"
      "    EmitVertex();\n"
      "    EndPrimitive();\n"
      "    gl_Position = gl_in[0].gl_Position;\n"
      "    gl_PointSize = gl_in[0].gl_PointSize;\n"
      "    out_vtx = in_vtx[0];\n"
      "    EmitVertex();\n"
      "    gl_Position = gl_in[2].gl_Position;\n"
      "    gl_PointSize = gl_in[2].gl_PointSize;\n"
      "    out_vtx = in_vtx[2];\n"
      "    EmitVertex();\n"
      "    gl_Position = (gl_in[0].gl_Position + gl_in[2].gl_Position) - \n"
      "        gl_in[1].gl_Position;\n"
      "    gl_PointSize = gl_in[2].gl_PointSize;\n"
      "    for (int i = 0; i < 16; ++i) {\n"
      "      out_vtx.o[i] = in_vtx[0].o[i] + -in_vtx[1].o[i] + \n"
      "          in_vtx[2].o[i];\n"
      "    }\n"
      "    EmitVertex();\n"
      "    EndPrimitive();\n"
      "  }\n"
      "}\n";
  std::string quad_list_shader =
      geometry_header +
      "layout(lines_adjacency) in;\n"
      "layout(triangle_strip, max_vertices = 4) out;\n"
      "void main() {\n"
      "  const int order[4] = { 0, 1, 3, 2 };\n"
      "  for (int i = 0; i < 4; ++i) {\n"
      "    int input_index = order[i];\n"
      "    gl_Position = gl_in[input_index].gl_Position;\n"
      "    gl_PointSize = gl_in[input_index].gl_PointSize;\n"
      "    out_vtx = in_vtx[input_index];\n"
      "    EmitVertex();\n"
      "  }\n"
      "  EndPrimitive();\n"
      "}\n";
  std::string line_quad_list_shader =
      geometry_header +
      "layout(lines_adjacency) in;\n"
      "layout(line_strip, max_vertices = 5) out;\n"
      "void main() {\n"
      "  gl_Position = gl_in[0].gl_Position;\n"
      "  gl_PointSize = gl_in[0].gl_PointSize;\n"
      "  out_vtx = in_vtx[0];\n"
      "  EmitVertex();\n"
      "  gl_Position = gl_in[1].gl_Position;\n"
      "  gl_PointSize = gl_in[1].gl_PointSize;\n"
      "  out_vtx = in_vtx[1];\n"
      "  EmitVertex();\n"
      "  gl_Position = gl_in[2].gl_Position;\n"
      "  gl_PointSize = gl_in[2].gl_PointSize;\n"
      "  out_vtx = in_vtx[2];\n"
      "  EmitVertex();\n"
      "  gl_Position = gl_in[3].gl_Position;\n"
      "  gl_PointSize = gl_in[3].gl_PointSize;\n"
      "  out_vtx = in_vtx[3];\n"
      "  EmitVertex();\n"
      "  gl_Position = gl_in[0].gl_Position;\n"
      "  gl_PointSize = gl_in[0].gl_PointSize;\n"
      "  out_vtx = in_vtx[0];\n"
      "  EmitVertex();\n"
      "  EndPrimitive();\n"
      "}\n";
  point_list_geometry_program_ = CreateGeometryProgram(point_list_shader);
  rect_list_geometry_program_ = CreateGeometryProgram(rect_list_shader);
  quad_list_geometry_program_ = CreateGeometryProgram(quad_list_shader);
  line_quad_list_geometry_program_ =
      CreateGeometryProgram(line_quad_list_shader);
  if (!point_list_geometry_program_ || !rect_list_geometry_program_ ||
      !quad_list_geometry_program_ || !line_quad_list_geometry_program_) {
    return false;
  }

  glEnable(GL_SCISSOR_TEST);
  glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
  glPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, GL_UPPER_LEFT);

  return true;
}

GLuint GL4CommandProcessor::CreateGeometryProgram(const std::string& source) {
  auto source_str = source.c_str();
  GLuint program = glCreateShaderProgramv(GL_GEOMETRY_SHADER, 1, &source_str);

  // Get error log, if we failed to link.
  GLint link_status = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &link_status);
  if (!link_status) {
    GLint log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    std::string info_log;
    info_log.resize(log_length - 1);
    glGetProgramInfoLog(program, log_length, &log_length,
                        const_cast<char*>(info_log.data()));
    XELOGE("Unable to link program: %s", info_log.c_str());
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

void GL4CommandProcessor::ShutdownContext() {
  glDeleteProgram(point_list_geometry_program_);
  glDeleteProgram(rect_list_geometry_program_);
  glDeleteProgram(quad_list_geometry_program_);
  glDeleteProgram(line_quad_list_geometry_program_);
  texture_cache_.Shutdown();
  draw_batcher_.Shutdown();
  scratch_buffer_.Shutdown();

  all_pipelines_.clear();
  shader_cache_.Reset();

  CommandProcessor::ShutdownContext();
}

void GL4CommandProcessor::MakeCoherent() {
  RegisterFile* regs = register_file_;
  auto status_host = regs->values[XE_GPU_REG_COHER_STATUS_HOST].u32;

  CommandProcessor::MakeCoherent();

  if (status_host & 0x80000000ul) {
    scratch_buffer_.ClearCache();
  }
}

void GL4CommandProcessor::PrepareForWait() {
  SCOPE_profile_cpu_f("gpu");

  CommandProcessor::PrepareForWait();

  // TODO(benvanik): fences and fancy stuff. We should figure out a way to
  // make interrupt callbacks from the GPU so that we don't have to do a full
  // synchronize here.
  glFlush();
  // glFinish();

  if (FLAGS_thread_safe_gl) {
    context_->ClearCurrent();
  }
}

void GL4CommandProcessor::ReturnFromWait() {
  if (FLAGS_thread_safe_gl) {
    context_->MakeCurrent();
  }

  CommandProcessor::ReturnFromWait();
}

void GL4CommandProcessor::PerformSwap(uint32_t frontbuffer_ptr,
                                      uint32_t frontbuffer_width,
                                      uint32_t frontbuffer_height) {
  // Ensure we issue any pending draws.
  draw_batcher_.Flush(DrawBatcher::FlushMode::kMakeCoherent);

  // One-time initialization.
  // TODO(benvanik): move someplace more sane?
  if (!swap_state_.front_buffer_texture) {
    std::lock_guard<std::mutex> lock(swap_state_.mutex);
    swap_state_.width = frontbuffer_width;
    swap_state_.height = frontbuffer_height;
    GLuint front_buffer_texture;
    GLuint back_buffer_texture;
    glCreateTextures(GL_TEXTURE_2D, 1, &front_buffer_texture);
    glCreateTextures(GL_TEXTURE_2D, 1, &back_buffer_texture);
    swap_state_.front_buffer_texture = front_buffer_texture;
    swap_state_.back_buffer_texture = back_buffer_texture;
    glTextureStorage2D(front_buffer_texture, 1, GL_RGBA8, swap_state_.width,
                       swap_state_.height);
    glTextureStorage2D(back_buffer_texture, 1, GL_RGBA8, swap_state_.width,
                       swap_state_.height);
  }

  // Lookup the framebuffer in the recently-resolved list.
  // TODO(benvanik): make this much more sophisticated.
  // TODO(benvanik): handle not found cases.
  // TODO(benvanik): handle dirty cases (resolved to sysmem, touched).
  // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  // HACK: just use whatever our current framebuffer is.
  GLuint framebuffer_texture = last_framebuffer_texture_;

  if (last_framebuffer_texture_ == 0) {
    framebuffer_texture =
        active_framebuffer_ ? active_framebuffer_->color_targets[0] : 0;
  }

  // Copy the the given framebuffer to the current backbuffer.
  Rect2D src_rect(0, 0, frontbuffer_width ? frontbuffer_width : 1280,
                  frontbuffer_height ? frontbuffer_height : 720);
  Rect2D dest_rect(0, 0, swap_state_.width, swap_state_.height);
  if (framebuffer_texture != 0) {
    reinterpret_cast<xe::ui::gl::GLContext*>(context_.get())
        ->blitter()
        ->CopyColorTexture2D(
            framebuffer_texture, src_rect,
            static_cast<GLuint>(swap_state_.back_buffer_texture), dest_rect,
            GL_LINEAR, true);
  }

  if (FLAGS_draw_all_framebuffers) {
    int32_t offsetx = (1280 - (1280 / 5));
    int32_t offsety = 0;
    int32_t doffsetx = 0;
    for (int i = 0; i < cached_framebuffers_.size(); i++) {
      bool has_colortargets = false;

      // Copy color targets to top right corner
      for (int j = 0; j < 4; j++) {
        GLuint tex = cached_framebuffers_[i].color_targets[j];
        if (!tex) {
          continue;
        }
        has_colortargets = true;

        dest_rect = {offsetx, offsety, 1280 / 5, 720 / 5};
        reinterpret_cast<ui::gl::GLContext*>(context_.get())
            ->blitter()
            ->CopyColorTexture2D(
                tex, src_rect,
                static_cast<GLuint>(swap_state_.back_buffer_texture), dest_rect,
                GL_LINEAR, true);

        offsety += 720 / 5;
      }

      if (has_colortargets) {
        offsetx -= 1280 / 5;
      }

      offsety = 0;

      GLuint tex = cached_framebuffers_[i].depth_target;
      if (!tex) {
        continue;
      }

      // Copy depth targets to bottom left corner of screen
      dest_rect = {doffsetx, (int32_t)swap_state_.height - (720 / 5), 1280 / 5,
                   720 / 5};
      reinterpret_cast<ui::gl::GLContext*>(context_.get())
          ->blitter()
          ->CopyColorTexture2D(
              tex, src_rect,
              static_cast<GLuint>(swap_state_.back_buffer_texture), dest_rect,
              GL_LINEAR, false);

      doffsetx += 1280 / 5;
    }
  }

  // Need to finish to be sure the other context sees the right data.
  // TODO(benvanik): prevent this? fences?
  glFinish();

  if (context_->WasLost()) {
    // We've lost the context due to a TDR.
    // TODO: Dump the current commands to a tracefile.
    assert_always();
  }

  // Remove any dead textures, etc.
  texture_cache_.Scavenge();
}

Shader* GL4CommandProcessor::LoadShader(ShaderType shader_type,
                                        uint32_t guest_address,
                                        const uint32_t* host_address,
                                        uint32_t dword_count) {
  return shader_cache_.LookupOrInsertShader(shader_type, host_address,
                                            dword_count);
}

bool GL4CommandProcessor::IssueDraw(PrimitiveType prim_type,
                                    uint32_t index_count,
                                    IndexBufferInfo* index_buffer_info) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  bool draw_valid;
  if (index_buffer_info) {
    draw_valid = draw_batcher_.BeginDrawElements(prim_type, index_count,
                                                 index_buffer_info->format);
  } else {
    draw_valid = draw_batcher_.BeginDrawArrays(prim_type, index_count);
  }
  if (!draw_valid) {
    return false;
  }

  auto& regs = *register_file_;

  auto enable_mode =
      static_cast<ModeControl>(regs[XE_GPU_REG_RB_MODECONTROL].u32 & 0x7);
  if (enable_mode == ModeControl::kIgnore) {
    // Ignored.
    draw_batcher_.DiscardDraw();
    return true;
  } else if (enable_mode == ModeControl::kCopy) {
    // Special copy handling.
    draw_batcher_.DiscardDraw();
    return IssueCopy();
  }

#define CHECK_ISSUE_UPDATE_STATUS(status, mismatch, error_message) \
  {                                                                \
    if (status == UpdateStatus::kError) {                          \
      XELOGE(error_message);                                       \
      draw_batcher_.DiscardDraw();                                 \
      return false;                                                \
    } else if (status == UpdateStatus::kMismatch) {                \
      mismatch = true;                                             \
    }                                                              \
  }

  UpdateStatus status;
  bool mismatch = false;
  status = UpdateShaders(draw_batcher_.prim_type());
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to prepare draw shaders");
  status = UpdateRenderTargets();
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to setup render targets");
  if (!active_framebuffer_) {
    // No framebuffer, so nothing we do will actually have an effect.
    // Treat it as a no-op.
    // TODO(benvanik): if we have a vs export, still allow it to go.
    draw_batcher_.DiscardDraw();
    return true;
  }

  status = UpdateState(draw_batcher_.prim_type());
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to setup render state");
  status = PopulateSamplers();
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch,
                            "Unable to prepare draw samplers");

  status = PopulateIndexBuffer(index_buffer_info);
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to setup index buffer");
  status = PopulateVertexBuffers();
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to setup vertex buffers");

  if (!draw_batcher_.CommitDraw()) {
    return false;
  }

  // TODO(benvanik): find a way to get around glVertexArrayVertexBuffer below.
  draw_batcher_.Flush(DrawBatcher::FlushMode::kMakeCoherent);
  if (context_->WasLost()) {
    // This draw lost us the context. This typically isn't hit.
    assert_always();
    return false;
  }

  return true;
}

bool GL4CommandProcessor::SetShadowRegister(uint32_t* dest,
                                            uint32_t register_name) {
  uint32_t value = register_file_->values[register_name].u32;
  if (*dest == value) {
    return false;
  }
  *dest = value;
  return true;
}

bool GL4CommandProcessor::SetShadowRegister(float* dest,
                                            uint32_t register_name) {
  float value = register_file_->values[register_name].f32;
  if (*dest == value) {
    return false;
  }
  *dest = value;
  return true;
}

GL4CommandProcessor::UpdateStatus GL4CommandProcessor::UpdateShaders(
    PrimitiveType prim_type) {
  auto& regs = update_shaders_regs_;

  // These are the constant base addresses/ranges for shaders.
  // We have these hardcoded right now cause nothing seems to differ.
  assert_true(register_file_->values[XE_GPU_REG_SQ_VS_CONST].u32 ==
                  0x000FF000 ||
              register_file_->values[XE_GPU_REG_SQ_VS_CONST].u32 == 0x00000000);
  assert_true(register_file_->values[XE_GPU_REG_SQ_PS_CONST].u32 ==
                  0x000FF100 ||
              register_file_->values[XE_GPU_REG_SQ_PS_CONST].u32 == 0x00000000);

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.pa_su_sc_mode_cntl,
                             XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(&regs.sq_program_cntl, XE_GPU_REG_SQ_PROGRAM_CNTL);
  dirty |= SetShadowRegister(&regs.sq_context_misc, XE_GPU_REG_SQ_CONTEXT_MISC);
  dirty |= regs.vertex_shader != active_vertex_shader_;
  dirty |= regs.pixel_shader != active_pixel_shader_;
  dirty |= regs.prim_type != prim_type;
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }
  regs.vertex_shader = static_cast<GL4Shader*>(active_vertex_shader_);
  regs.pixel_shader = static_cast<GL4Shader*>(active_pixel_shader_);
  regs.prim_type = prim_type;

  SCOPE_profile_cpu_f("gpu");

  draw_batcher_.Flush(DrawBatcher::FlushMode::kStateChange);

  xe_gpu_program_cntl_t program_cntl;
  program_cntl.dword_0 = regs.sq_program_cntl;

  // Populate a register in the pixel shader with frag coord.
  int ps_param_gen = (regs.sq_context_misc >> 8) & 0xFF;
  draw_batcher_.set_ps_param_gen(program_cntl.param_gen ? ps_param_gen : -1);

  // Normal vertex shaders only, for now.
  // TODO(benvanik): transform feedback/memexport.
  // https://github.com/freedreno/freedreno/blob/master/includes/a2xx.xml.h
  // 0 = normal
  // 2 = point size
  assert_true(program_cntl.vs_export_mode == 0 ||
              program_cntl.vs_export_mode == 2);

  if (!regs.vertex_shader->is_valid()) {
    XELOGE("Vertex shader invalid");
    return UpdateStatus::kError;
  }
  if (!regs.pixel_shader->is_valid()) {
    XELOGE("Pixel shader invalid");
    return UpdateStatus::kError;
  }

  GLuint vertex_program = regs.vertex_shader->program();
  GLuint fragment_program = regs.pixel_shader->program();

  uint64_t key = (uint64_t(vertex_program) << 32) | fragment_program;
  CachedPipeline* cached_pipeline = nullptr;
  auto it = cached_pipelines_.find(key);
  if (it == cached_pipelines_.end()) {
    // Existing pipeline for these programs not found - create it.
    auto new_pipeline = std::make_unique<CachedPipeline>();
    new_pipeline->vertex_program = vertex_program;
    new_pipeline->fragment_program = fragment_program;
    new_pipeline->handles.default_pipeline = 0;
    cached_pipeline = new_pipeline.get();
    all_pipelines_.emplace_back(std::move(new_pipeline));
    cached_pipelines_.insert({key, cached_pipeline});
  } else {
    // Found a pipeline container - it may or may not have what we want.
    cached_pipeline = it->second;
  }
  if (!cached_pipeline->handles.default_pipeline) {
    // Perhaps it's a bit wasteful to do all of these, but oh well.
    GLuint pipelines[5];
    glCreateProgramPipelines(GLsizei(xe::countof(pipelines)), pipelines);

    glUseProgramStages(pipelines[0], GL_VERTEX_SHADER_BIT, vertex_program);
    glUseProgramStages(pipelines[0], GL_FRAGMENT_SHADER_BIT, fragment_program);
    cached_pipeline->handles.default_pipeline = pipelines[0];

    glUseProgramStages(pipelines[1], GL_VERTEX_SHADER_BIT, vertex_program);
    glUseProgramStages(pipelines[1], GL_GEOMETRY_SHADER_BIT,
                       point_list_geometry_program_);
    glUseProgramStages(pipelines[1], GL_FRAGMENT_SHADER_BIT, fragment_program);
    cached_pipeline->handles.point_list_pipeline = pipelines[1];

    glUseProgramStages(pipelines[2], GL_VERTEX_SHADER_BIT, vertex_program);
    glUseProgramStages(pipelines[2], GL_GEOMETRY_SHADER_BIT,
                       rect_list_geometry_program_);
    glUseProgramStages(pipelines[2], GL_FRAGMENT_SHADER_BIT, fragment_program);
    cached_pipeline->handles.rect_list_pipeline = pipelines[2];

    glUseProgramStages(pipelines[3], GL_VERTEX_SHADER_BIT, vertex_program);
    glUseProgramStages(pipelines[3], GL_GEOMETRY_SHADER_BIT,
                       quad_list_geometry_program_);
    glUseProgramStages(pipelines[3], GL_FRAGMENT_SHADER_BIT, fragment_program);
    cached_pipeline->handles.quad_list_pipeline = pipelines[3];

    glUseProgramStages(pipelines[4], GL_VERTEX_SHADER_BIT, vertex_program);
    glUseProgramStages(pipelines[4], GL_GEOMETRY_SHADER_BIT,
                       line_quad_list_geometry_program_);
    glUseProgramStages(pipelines[4], GL_FRAGMENT_SHADER_BIT, fragment_program);
    cached_pipeline->handles.line_quad_list_pipeline = pipelines[4];

    // This can be set once, as the buffer never changes.
    glVertexArrayElementBuffer(regs.vertex_shader->vao(),
                               scratch_buffer_.handle());
  }

  bool line_mode = false;
  if (((regs.pa_su_sc_mode_cntl >> 3) & 0x3) != 0) {
    uint32_t front_poly_mode = (regs.pa_su_sc_mode_cntl >> 5) & 0x7;
    if (front_poly_mode == 1) {
      line_mode = true;
    }
  }

  GLuint pipeline;
  switch (regs.prim_type) {
    default:
      // Default pipeline used.
      pipeline = cached_pipeline->handles.default_pipeline;
      break;
    case PrimitiveType::kPointList:
      pipeline = cached_pipeline->handles.point_list_pipeline;
      break;
    case PrimitiveType::kRectangleList:
      pipeline = cached_pipeline->handles.rect_list_pipeline;
      break;
    case PrimitiveType::kQuadList: {
      if (line_mode) {
        pipeline = cached_pipeline->handles.line_quad_list_pipeline;
      } else {
        pipeline = cached_pipeline->handles.quad_list_pipeline;
      }
      break;
    }
  }

  draw_batcher_.ReconfigurePipeline(regs.vertex_shader, regs.pixel_shader,
                                    pipeline);

  glBindProgramPipeline(pipeline);
  glBindVertexArray(regs.vertex_shader->vao());

  return UpdateStatus::kMismatch;
}

GL4CommandProcessor::UpdateStatus GL4CommandProcessor::UpdateRenderTargets() {
  auto& regs = update_render_targets_regs_;

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.rb_modecontrol, XE_GPU_REG_RB_MODECONTROL);
  dirty |= SetShadowRegister(&regs.rb_surface_info, XE_GPU_REG_RB_SURFACE_INFO);
  dirty |= SetShadowRegister(&regs.rb_color_info, XE_GPU_REG_RB_COLOR_INFO);
  dirty |= SetShadowRegister(&regs.rb_color1_info, XE_GPU_REG_RB_COLOR1_INFO);
  dirty |= SetShadowRegister(&regs.rb_color2_info, XE_GPU_REG_RB_COLOR2_INFO);
  dirty |= SetShadowRegister(&regs.rb_color3_info, XE_GPU_REG_RB_COLOR3_INFO);
  dirty |= SetShadowRegister(&regs.rb_color_mask, XE_GPU_REG_RB_COLOR_MASK);
  dirty |= SetShadowRegister(&regs.rb_depthcontrol, XE_GPU_REG_RB_DEPTHCONTROL);
  dirty |=
      SetShadowRegister(&regs.rb_stencilrefmask, XE_GPU_REG_RB_STENCILREFMASK);
  dirty |= SetShadowRegister(&regs.rb_depth_info, XE_GPU_REG_RB_DEPTH_INFO);
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  SCOPE_profile_cpu_f("gpu");

  draw_batcher_.Flush(DrawBatcher::FlushMode::kStateChange);

  auto enable_mode = static_cast<ModeControl>(regs.rb_modecontrol & 0x7);

  // RB_SURFACE_INFO
  // http://fossies.org/dox/MesaLib-10.3.5/fd2__gmem_8c_source.html
  uint32_t surface_pitch = regs.rb_surface_info & 0x3FFF;
  auto surface_msaa =
      static_cast<MsaaSamples>((regs.rb_surface_info >> 16) & 0x3);

  // Get/create all color render targets, if we are using them.
  // In depth-only mode we don't need them.
  // Note that write mask may be more permissive than we want, so we mix that
  // with the actual targets the pixel shader writes to.
  GLenum draw_buffers[4] = {GL_NONE, GL_NONE, GL_NONE, GL_NONE};
  GLuint color_targets[4] = {kAnyTarget, kAnyTarget, kAnyTarget, kAnyTarget};
  if (enable_mode == ModeControl::kColorDepth) {
    uint32_t color_info[4] = {
        regs.rb_color_info,
        regs.rb_color1_info,
        regs.rb_color2_info,
        regs.rb_color3_info,
    };
    // A2XX_RB_COLOR_MASK_WRITE_* == D3DRS_COLORWRITEENABLE
    for (int n = 0; n < xe::countof(color_info); n++) {
      uint32_t write_mask = (regs.rb_color_mask >> (n * 4)) & 0xF;
      if (!write_mask || !active_pixel_shader_->writes_color_target(n)) {
        // Unused, so keep disabled and set to wildcard so we'll take any
        // framebuffer that has it.
        continue;
      }
      uint32_t color_base = color_info[n] & 0xFFF;
      auto color_format =
          static_cast<ColorRenderTargetFormat>((color_info[n] >> 16) & 0xF);
      color_targets[n] = GetColorRenderTarget(surface_pitch, surface_msaa,
                                              color_base, color_format);
      draw_buffers[n] = GL_COLOR_ATTACHMENT0 + n;
      glColorMaski(n, !!(write_mask & 0x1), !!(write_mask & 0x2),
                   !!(write_mask & 0x4), !!(write_mask & 0x8));
    }
  }

  // Get/create depth buffer, but only if we are going to use it.
  bool uses_depth = (regs.rb_depthcontrol & 0x00000002) ||
                    (regs.rb_depthcontrol & 0x00000004);
  uint32_t stencil_write_mask = (regs.rb_stencilrefmask & 0x00FF0000) >> 16;
  bool uses_stencil =
      (regs.rb_depthcontrol & 0x00000001) || (stencil_write_mask != 0);
  GLuint depth_target = kAnyTarget;
  if (uses_depth || uses_stencil) {
    uint32_t depth_base = regs.rb_depth_info & 0xFFF;
    auto depth_format =
        static_cast<DepthRenderTargetFormat>((regs.rb_depth_info >> 16) & 0x1);
    depth_target = GetDepthRenderTarget(surface_pitch, surface_msaa, depth_base,
                                        depth_format);
    // TODO(benvanik): when a game switches does it expect to keep the same
    //     depth buffer contents?
  }

  // Get/create a framebuffer with the required targets.
  // Note that none may be returned if we really don't need one.
  auto cached_framebuffer = GetFramebuffer(color_targets, depth_target);
  active_framebuffer_ = cached_framebuffer;
  if (active_framebuffer_) {
    // Setup just the targets we want.
    glNamedFramebufferDrawBuffers(cached_framebuffer->framebuffer, 4,
                                  draw_buffers);

    // Make active.
    // TODO(benvanik): can we do this all named?
    // TODO(benvanik): do we want this on READ too?
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, cached_framebuffer->framebuffer);
  }

  return UpdateStatus::kMismatch;
}

GL4CommandProcessor::UpdateStatus GL4CommandProcessor::UpdateState(
    PrimitiveType prim_type) {
  bool mismatch = false;

#define CHECK_UPDATE_STATUS(status, mismatch, error_message) \
  {                                                          \
    if (status == UpdateStatus::kError) {                    \
      XELOGE(error_message);                                 \
      return status;                                         \
    } else if (status == UpdateStatus::kMismatch) {          \
      mismatch = true;                                       \
    }                                                        \
  }

  UpdateStatus status;
  status = UpdateViewportState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update viewport state");
  status = UpdateRasterizerState(prim_type);
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update rasterizer state");
  status = UpdateBlendState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update blend state");
  status = UpdateDepthStencilState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update depth/stencil state");

  return mismatch ? UpdateStatus::kMismatch : UpdateStatus::kCompatible;
}

GL4CommandProcessor::UpdateStatus GL4CommandProcessor::UpdateViewportState() {
  auto& regs = update_viewport_state_regs_;

  bool dirty = false;
  // dirty |= SetShadowRegister(&state_regs.pa_cl_clip_cntl,
  //     XE_GPU_REG_PA_CL_CLIP_CNTL);
  dirty |= SetShadowRegister(&regs.rb_surface_info, XE_GPU_REG_RB_SURFACE_INFO);
  dirty |= SetShadowRegister(&regs.pa_cl_vte_cntl, XE_GPU_REG_PA_CL_VTE_CNTL);
  dirty |= SetShadowRegister(&regs.pa_su_sc_mode_cntl,
                             XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(&regs.pa_sc_window_offset,
                             XE_GPU_REG_PA_SC_WINDOW_OFFSET);
  dirty |= SetShadowRegister(&regs.pa_sc_window_scissor_tl,
                             XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL);
  dirty |= SetShadowRegister(&regs.pa_sc_window_scissor_br,
                             XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_xoffset,
                             XE_GPU_REG_PA_CL_VPORT_XOFFSET);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_yoffset,
                             XE_GPU_REG_PA_CL_VPORT_YOFFSET);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_zoffset,
                             XE_GPU_REG_PA_CL_VPORT_ZOFFSET);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_xscale,
                             XE_GPU_REG_PA_CL_VPORT_XSCALE);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_yscale,
                             XE_GPU_REG_PA_CL_VPORT_YSCALE);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_zscale,
                             XE_GPU_REG_PA_CL_VPORT_ZSCALE);

  // Much of this state machine is extracted from:
  // https://github.com/freedreno/mesa/blob/master/src/mesa/drivers/dri/r200/r200_state.c
  // http://fossies.org/dox/MesaLib-10.3.5/fd2__gmem_8c_source.html
  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf

  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf
  // VTX_XY_FMT = true: the incoming X, Y have already been multiplied by 1/W0.
  //            = false: multiply the X, Y coordinates by 1/W0.
  // VTX_Z_FMT = true: the incoming Z has already been multiplied by 1/W0.
  //           = false: multiply the Z coordinate by 1/W0.
  // VTX_W0_FMT = true: the incoming W0 is not 1/W0. Perform the reciprocal to
  //                    get 1/W0.
  draw_batcher_.set_vtx_fmt((regs.pa_cl_vte_cntl >> 8) & 0x1 ? 1.0f : 0.0f,
                            (regs.pa_cl_vte_cntl >> 9) & 0x1 ? 1.0f : 0.0f,
                            (regs.pa_cl_vte_cntl >> 10) & 0x1 ? 1.0f : 0.0f);

  // Done in VS, no need to flush state.
  if ((regs.pa_cl_vte_cntl & (1 << 0)) > 0) {
    draw_batcher_.set_window_scalar(1.0f, 1.0f);
  } else {
    draw_batcher_.set_window_scalar(1.0f / 2560.0f, -1.0f / 2560.0f);
  }

  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  draw_batcher_.Flush(DrawBatcher::FlushMode::kStateChange);

  // Clipping.
  // https://github.com/freedreno/amd-gpu/blob/master/include/reg/yamato/14/yamato_genenum.h#L1587
  // bool clip_enabled = ((regs.pa_cl_clip_cntl >> 17) & 0x1) == 0;
  // bool dx_clip = ((regs.pa_cl_clip_cntl >> 19) & 0x1) == 0x1;
  //// TODO(benvanik): depth range?
  // if (dx_clip) {
  //  glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
  //} else {
  //  glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
  //}

  // Window parameters.
  // http://ftp.tku.edu.tw/NetBSD/NetBSD-current/xsrc/external/mit/xf86-video-ati/dist/src/r600_reg_auto_r6xx.h
  // See r200UpdateWindow:
  // https://github.com/freedreno/mesa/blob/master/src/mesa/drivers/dri/r200/r200_state.c
  int16_t window_offset_x = 0;
  int16_t window_offset_y = 0;
  if ((regs.pa_su_sc_mode_cntl >> 16) & 1) {
    window_offset_x = regs.pa_sc_window_offset & 0x7FFF;
    window_offset_y = (regs.pa_sc_window_offset >> 16) & 0x7FFF;
    if (window_offset_x & 0x4000) {
      window_offset_x |= 0x8000;
    }
    if (window_offset_y & 0x4000) {
      window_offset_y |= 0x8000;
    }
  }

  GLint ws_x = regs.pa_sc_window_scissor_tl & 0x7FFF;
  GLint ws_y = (regs.pa_sc_window_scissor_tl >> 16) & 0x7FFF;
  GLsizei ws_w = (regs.pa_sc_window_scissor_br & 0x7FFF) - ws_x;
  GLsizei ws_h = ((regs.pa_sc_window_scissor_br >> 16) & 0x7FFF) - ws_y;
  ws_x += window_offset_x;
  ws_y += window_offset_y;
  glScissorIndexed(0, ws_x, ws_y, ws_w, ws_h);

  // HACK: no clue where to get these values.
  // RB_SURFACE_INFO
  auto surface_msaa =
      static_cast<MsaaSamples>((regs.rb_surface_info >> 16) & 0x3);
  // TODO(benvanik): ??
  float window_width_scalar = 1;
  float window_height_scalar = 1;
  switch (surface_msaa) {
    case MsaaSamples::k1X:
      break;
    case MsaaSamples::k2X:
      window_width_scalar = 2;
      break;
    case MsaaSamples::k4X:
      window_width_scalar = 2;
      window_height_scalar = 2;
      break;
  }

  // Whether each of the viewport settings are enabled.
  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf
  bool vport_xscale_enable = (regs.pa_cl_vte_cntl & (1 << 0)) > 0;
  bool vport_xoffset_enable = (regs.pa_cl_vte_cntl & (1 << 1)) > 0;
  bool vport_yscale_enable = (regs.pa_cl_vte_cntl & (1 << 2)) > 0;
  bool vport_yoffset_enable = (regs.pa_cl_vte_cntl & (1 << 3)) > 0;
  bool vport_zscale_enable = (regs.pa_cl_vte_cntl & (1 << 4)) > 0;
  bool vport_zoffset_enable = (regs.pa_cl_vte_cntl & (1 << 5)) > 0;
  assert_true(vport_xscale_enable == vport_yscale_enable ==
              vport_zscale_enable == vport_xoffset_enable ==
              vport_yoffset_enable == vport_zoffset_enable);

  if (vport_xscale_enable) {
    float texel_offset_x = 0.0f;
    float texel_offset_y = 0.0f;
    float vox = vport_xoffset_enable ? regs.pa_cl_vport_xoffset : 0;
    float voy = vport_yoffset_enable ? regs.pa_cl_vport_yoffset : 0;
    float vsx = vport_xscale_enable ? regs.pa_cl_vport_xscale : 1;
    float vsy = vport_yscale_enable ? regs.pa_cl_vport_yscale : 1;
    window_width_scalar = window_height_scalar = 1;
    float vpw = 2 * window_width_scalar * vsx;
    float vph = -2 * window_height_scalar * vsy;
    float vpx = window_width_scalar * vox - vpw / 2 + window_offset_x;
    float vpy = window_height_scalar * voy - vph / 2 + window_offset_y;
    glViewportIndexedf(0, vpx + texel_offset_x, vpy + texel_offset_y, vpw, vph);

    // TODO(benvanik): depth range adjustment?
    // float voz = vport_zoffset_enable ? regs.pa_cl_vport_zoffset : 0;
    // float vsz = vport_zscale_enable ? regs.pa_cl_vport_zscale : 1;
  } else {
    float texel_offset_x = 0.0f;
    float texel_offset_y = 0.0f;
    float vpw = 2 * 2560.0f * window_width_scalar;
    float vph = 2 * 2560.0f * window_height_scalar;
    float vpx = -2560.0f * window_width_scalar + window_offset_x;
    float vpy = -2560.0f * window_height_scalar + window_offset_y;
    glViewportIndexedf(0, vpx + texel_offset_x, vpy + texel_offset_y, vpw, vph);
  }
  float voz = vport_zoffset_enable ? regs.pa_cl_vport_zoffset : 0;
  float vsz = vport_zscale_enable ? regs.pa_cl_vport_zscale : 1;
  glDepthRangef(voz, voz + vsz);

  return UpdateStatus::kMismatch;
}

GL4CommandProcessor::UpdateStatus GL4CommandProcessor::UpdateRasterizerState(
    PrimitiveType prim_type) {
  auto& regs = update_rasterizer_state_regs_;

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.pa_su_sc_mode_cntl,
                             XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(&regs.pa_sc_screen_scissor_tl,
                             XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL);
  dirty |= SetShadowRegister(&regs.pa_sc_screen_scissor_br,
                             XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR);
  dirty |= SetShadowRegister(&regs.multi_prim_ib_reset_index,
                             XE_GPU_REG_VGT_MULTI_PRIM_IB_RESET_INDX);
  dirty |= SetShadowRegister(&regs.pa_sc_viz_query, XE_GPU_REG_PA_SC_VIZ_QUERY);
  dirty |= regs.prim_type != prim_type;
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  regs.prim_type = prim_type;

  SCOPE_profile_cpu_f("gpu");

  draw_batcher_.Flush(DrawBatcher::FlushMode::kStateChange);

  // viz query enabled
  // assert_zero(regs.pa_sc_viz_query & 0x01);

  // Kill pix post early-z test
  // assert_zero(regs.pa_sc_viz_query & 0x80);

  // Scissoring.
  // TODO(benvanik): is this used? we are using scissoring for window scissor.
  if (regs.pa_sc_screen_scissor_tl != 0 &&
      regs.pa_sc_screen_scissor_br != 0x20002000) {
    assert_always();
    // glEnable(GL_SCISSOR_TEST);
    // TODO(benvanik): signed?
    int32_t screen_scissor_x = regs.pa_sc_screen_scissor_tl & 0x7FFF;
    int32_t screen_scissor_y = (regs.pa_sc_screen_scissor_tl >> 16) & 0x7FFF;
    int32_t screen_scissor_w =
        regs.pa_sc_screen_scissor_br & 0x7FFF - screen_scissor_x;
    int32_t screen_scissor_h =
        (regs.pa_sc_screen_scissor_br >> 16) & 0x7FFF - screen_scissor_y;
    glScissor(screen_scissor_x, screen_scissor_y, screen_scissor_w,
              screen_scissor_h);
  } else {
    // glDisable(GL_SCISSOR_TEST);
  }

  switch (regs.pa_su_sc_mode_cntl & 0x3) {
    case 0:
      glDisable(GL_CULL_FACE);
      break;
    case 1:
      glEnable(GL_CULL_FACE);
      glCullFace(GL_FRONT);
      break;
    case 2:
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
      break;
  }
  if (regs.pa_su_sc_mode_cntl & 0x4) {
    glFrontFace(GL_CW);
  } else {
    glFrontFace(GL_CCW);
  }

  if (prim_type == PrimitiveType::kRectangleList) {
    // Rectangle lists aren't culled. There may be other things they skip too.
    glDisable(GL_CULL_FACE);
  }

  static const GLenum kFillModes[3] = {
      GL_POINT,
      GL_LINE,
      GL_FILL,
  };
  bool poly_mode = ((regs.pa_su_sc_mode_cntl >> 3) & 0x3) != 0;
  if (poly_mode) {
    uint32_t front_poly_mode = (regs.pa_su_sc_mode_cntl >> 5) & 0x7;
    uint32_t back_poly_mode = (regs.pa_su_sc_mode_cntl >> 8) & 0x7;
    // GL only supports both matching.
    assert_true(front_poly_mode == back_poly_mode);
    glPolygonMode(GL_FRONT_AND_BACK, kFillModes[front_poly_mode]);
  } else {
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }

  if (regs.pa_su_sc_mode_cntl & (1 << 19)) {
    glProvokingVertex(GL_LAST_VERTEX_CONVENTION);
  } else {
    glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
  }

  if (regs.pa_su_sc_mode_cntl & (1 << 21)) {
    glEnable(GL_PRIMITIVE_RESTART);
  } else {
    glDisable(GL_PRIMITIVE_RESTART);
  }
  glPrimitiveRestartIndex(regs.multi_prim_ib_reset_index);

  return UpdateStatus::kMismatch;
}

GL4CommandProcessor::UpdateStatus GL4CommandProcessor::UpdateBlendState() {
  auto& reg_file = *register_file_;
  auto& regs = update_blend_state_regs_;

  // Alpha testing -- ALPHAREF, ALPHAFUNC, ALPHATESTENABLE
  // Deprecated in GL, implemented in shader.
  // if(ALPHATESTENABLE && frag_out.a [<=/ALPHAFUNC] ALPHAREF) discard;
  uint32_t color_control = reg_file[XE_GPU_REG_RB_COLORCONTROL].u32;
  draw_batcher_.set_alpha_test((color_control & 0x8) != 0,  // ALPAHTESTENABLE
                               color_control & 0x7,         // ALPHAFUNC
                               reg_file[XE_GPU_REG_RB_ALPHA_REF].f32);

  bool dirty = false;
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[0], XE_GPU_REG_RB_BLENDCONTROL_0);
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[1], XE_GPU_REG_RB_BLENDCONTROL_1);
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[2], XE_GPU_REG_RB_BLENDCONTROL_2);
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[3], XE_GPU_REG_RB_BLENDCONTROL_3);
  dirty |= SetShadowRegister(&regs.rb_blend_rgba[0], XE_GPU_REG_RB_BLEND_RED);
  dirty |= SetShadowRegister(&regs.rb_blend_rgba[1], XE_GPU_REG_RB_BLEND_GREEN);
  dirty |= SetShadowRegister(&regs.rb_blend_rgba[2], XE_GPU_REG_RB_BLEND_BLUE);
  dirty |= SetShadowRegister(&regs.rb_blend_rgba[3], XE_GPU_REG_RB_BLEND_ALPHA);
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  SCOPE_profile_cpu_f("gpu");

  draw_batcher_.Flush(DrawBatcher::FlushMode::kStateChange);

  static const GLenum blend_map[] = {
      /*  0 */ GL_ZERO,
      /*  1 */ GL_ONE,
      /*  2 */ GL_ZERO,  // ?
      /*  3 */ GL_ZERO,  // ?
      /*  4 */ GL_SRC_COLOR,
      /*  5 */ GL_ONE_MINUS_SRC_COLOR,
      /*  6 */ GL_SRC_ALPHA,
      /*  7 */ GL_ONE_MINUS_SRC_ALPHA,
      /*  8 */ GL_DST_COLOR,
      /*  9 */ GL_ONE_MINUS_DST_COLOR,
      /* 10 */ GL_DST_ALPHA,
      /* 11 */ GL_ONE_MINUS_DST_ALPHA,
      /* 12 */ GL_CONSTANT_COLOR,
      /* 13 */ GL_ONE_MINUS_CONSTANT_COLOR,
      /* 14 */ GL_CONSTANT_ALPHA,
      /* 15 */ GL_ONE_MINUS_CONSTANT_ALPHA,
      /* 16 */ GL_SRC_ALPHA_SATURATE,
  };
  static const GLenum blend_op_map[] = {
      /*  0 */ GL_FUNC_ADD,
      /*  1 */ GL_FUNC_SUBTRACT,
      /*  2 */ GL_MIN,
      /*  3 */ GL_MAX,
      /*  4 */ GL_FUNC_REVERSE_SUBTRACT,
  };
  for (int i = 0; i < xe::countof(regs.rb_blendcontrol); ++i) {
    uint32_t blend_control = regs.rb_blendcontrol[i];
    // A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND
    auto src_blend = blend_map[(blend_control & 0x0000001F) >> 0];
    // A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND
    auto dest_blend = blend_map[(blend_control & 0x00001F00) >> 8];
    // A2XX_RB_BLEND_CONTROL_COLOR_COMB_FCN
    auto blend_op = blend_op_map[(blend_control & 0x000000E0) >> 5];
    // A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND
    auto src_blend_alpha = blend_map[(blend_control & 0x001F0000) >> 16];
    // A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND
    auto dest_blend_alpha = blend_map[(blend_control & 0x1F000000) >> 24];
    // A2XX_RB_BLEND_CONTROL_ALPHA_COMB_FCN
    auto blend_op_alpha = blend_op_map[(blend_control & 0x00E00000) >> 21];
    // A2XX_RB_COLORCONTROL_BLEND_DISABLE ?? Can't find this!
    // Just guess based on actions.
    // bool blend_enable =
    //     !((src_blend == GL_ONE) && (dest_blend == GL_ZERO) &&
    //       (blend_op == GL_FUNC_ADD) && (src_blend_alpha == GL_ONE) &&
    //       (dest_blend_alpha == GL_ZERO) && (blend_op_alpha == GL_FUNC_ADD));
    bool blend_enable = !(color_control & 0x20);
    if (blend_enable) {
      glEnablei(GL_BLEND, i);
      glBlendEquationSeparatei(i, blend_op, blend_op_alpha);
      glBlendFuncSeparatei(i, src_blend, dest_blend, src_blend_alpha,
                           dest_blend_alpha);
    } else {
      glDisablei(GL_BLEND, i);
    }
  }

  glBlendColor(regs.rb_blend_rgba[0], regs.rb_blend_rgba[1],
               regs.rb_blend_rgba[2], regs.rb_blend_rgba[3]);

  return UpdateStatus::kMismatch;
}

GL4CommandProcessor::UpdateStatus
GL4CommandProcessor::UpdateDepthStencilState() {
  auto& regs = update_depth_stencil_state_regs_;

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.rb_depthcontrol, XE_GPU_REG_RB_DEPTHCONTROL);
  dirty |=
      SetShadowRegister(&regs.rb_stencilrefmask, XE_GPU_REG_RB_STENCILREFMASK);
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  SCOPE_profile_cpu_f("gpu");

  draw_batcher_.Flush(DrawBatcher::FlushMode::kStateChange);

  static const GLenum compare_func_map[] = {
      /*  0 */ GL_NEVER,
      /*  1 */ GL_LESS,
      /*  2 */ GL_EQUAL,
      /*  3 */ GL_LEQUAL,
      /*  4 */ GL_GREATER,
      /*  5 */ GL_NOTEQUAL,
      /*  6 */ GL_GEQUAL,
      /*  7 */ GL_ALWAYS,
  };
  static const GLenum stencil_op_map[] = {
      /*  0 */ GL_KEEP,
      /*  1 */ GL_ZERO,
      /*  2 */ GL_REPLACE,
      /*  3 */ GL_INCR_WRAP,
      /*  4 */ GL_DECR_WRAP,
      /*  5 */ GL_INVERT,
      /*  6 */ GL_INCR,
      /*  7 */ GL_DECR,
  };
  // A2XX_RB_DEPTHCONTROL_Z_ENABLE
  if (regs.rb_depthcontrol & 0x00000002) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }
  // glDisable(GL_DEPTH_TEST);
  // A2XX_RB_DEPTHCONTROL_Z_WRITE_ENABLE
  glDepthMask((regs.rb_depthcontrol & 0x00000004) ? GL_TRUE : GL_FALSE);
  // A2XX_RB_DEPTHCONTROL_EARLY_Z_ENABLE
  // ?
  // A2XX_RB_DEPTHCONTROL_ZFUNC
  glDepthFunc(compare_func_map[(regs.rb_depthcontrol & 0x00000070) >> 4]);
  // A2XX_RB_DEPTHCONTROL_STENCIL_ENABLE
  if (regs.rb_depthcontrol & 0x00000001) {
    glEnable(GL_STENCIL_TEST);
  } else {
    glDisable(GL_STENCIL_TEST);
  }
  // RB_STENCILREFMASK_STENCILREF
  uint32_t stencil_ref = (regs.rb_stencilrefmask & 0x000000FF);
  // RB_STENCILREFMASK_STENCILMASK
  uint32_t stencil_read_mask = (regs.rb_stencilrefmask & 0x0000FF00) >> 8;
  // RB_STENCILREFMASK_STENCILWRITEMASK
  glStencilMask((regs.rb_stencilrefmask & 0x00FF0000) >> 16);
  // A2XX_RB_DEPTHCONTROL_BACKFACE_ENABLE
  bool backface_enabled = (regs.rb_depthcontrol & 0x00000080) != 0;
  if (backface_enabled) {
    // A2XX_RB_DEPTHCONTROL_STENCILFUNC
    glStencilFuncSeparate(
        GL_FRONT, compare_func_map[(regs.rb_depthcontrol & 0x00000700) >> 8],
        stencil_ref, stencil_read_mask);
    // A2XX_RB_DEPTHCONTROL_STENCILFAIL
    // A2XX_RB_DEPTHCONTROL_STENCILZFAIL
    // A2XX_RB_DEPTHCONTROL_STENCILZPASS
    glStencilOpSeparate(
        GL_FRONT, stencil_op_map[(regs.rb_depthcontrol & 0x00003800) >> 11],
        stencil_op_map[(regs.rb_depthcontrol & 0x000E0000) >> 17],
        stencil_op_map[(regs.rb_depthcontrol & 0x0001C000) >> 14]);
    // A2XX_RB_DEPTHCONTROL_STENCILFUNC_BF
    glStencilFuncSeparate(
        GL_BACK, compare_func_map[(regs.rb_depthcontrol & 0x00700000) >> 20],
        stencil_ref, stencil_read_mask);
    // A2XX_RB_DEPTHCONTROL_STENCILFAIL_BF
    // A2XX_RB_DEPTHCONTROL_STENCILZFAIL_BF
    // A2XX_RB_DEPTHCONTROL_STENCILZPASS_BF
    glStencilOpSeparate(
        GL_BACK, stencil_op_map[(regs.rb_depthcontrol & 0x03800000) >> 23],
        stencil_op_map[(regs.rb_depthcontrol & 0xE0000000) >> 29],
        stencil_op_map[(regs.rb_depthcontrol & 0x1C000000) >> 26]);
  } else {
    // Backfaces disabled - treat backfaces as frontfaces.
    glStencilFunc(compare_func_map[(regs.rb_depthcontrol & 0x00000700) >> 8],
                  stencil_ref, stencil_read_mask);
    glStencilOp(stencil_op_map[(regs.rb_depthcontrol & 0x00003800) >> 11],
                stencil_op_map[(regs.rb_depthcontrol & 0x000E0000) >> 17],
                stencil_op_map[(regs.rb_depthcontrol & 0x0001C000) >> 14]);
  }

  return UpdateStatus::kMismatch;
}

GL4CommandProcessor::UpdateStatus GL4CommandProcessor::PopulateIndexBuffer(
    IndexBufferInfo* index_buffer_info) {
  auto& regs = *register_file_;
  if (!index_buffer_info || !index_buffer_info->guest_base) {
    // No index buffer or auto draw.
    return UpdateStatus::kCompatible;
  }
  auto& info = *index_buffer_info;

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  // Min/max index ranges for clamping. This is often [0g,FFFF|FFFFFF].
  // All indices should be clamped to [min,max]. May be a way to do this in GL.
  uint32_t min_index = regs[XE_GPU_REG_VGT_MIN_VTX_INDX].u32;
  uint32_t max_index = regs[XE_GPU_REG_VGT_MAX_VTX_INDX].u32;
  assert_true(min_index == 0);
  assert_true(max_index == 0xFFFF || max_index == 0xFFFFFF);

  assert_true(info.endianness == Endian::k8in16 ||
              info.endianness == Endian::k8in32);

  trace_writer_.WriteMemoryRead(info.guest_base, info.length);

  size_t total_size =
      info.count * (info.format == IndexFormat::kInt32 ? sizeof(uint32_t)
                                                       : sizeof(uint16_t));
  CircularBuffer::Allocation allocation;
  if (!scratch_buffer_.AcquireCached(info.guest_base, total_size,
                                     &allocation)) {
    if (info.format == IndexFormat::kInt32) {
      auto dest = reinterpret_cast<uint32_t*>(allocation.host_ptr);
      auto src = memory_->TranslatePhysical<const uint32_t*>(info.guest_base);
      xe::copy_and_swap_32_aligned(dest, src, info.count);
    } else {
      auto dest = reinterpret_cast<uint16_t*>(allocation.host_ptr);
      auto src = memory_->TranslatePhysical<const uint16_t*>(info.guest_base);
      xe::copy_and_swap_16_aligned(dest, src, info.count);
    }
    draw_batcher_.set_index_buffer(allocation);
    scratch_buffer_.Commit(std::move(allocation));
  } else {
    draw_batcher_.set_index_buffer(allocation);
  }

  return UpdateStatus::kCompatible;
}

GL4CommandProcessor::UpdateStatus GL4CommandProcessor::PopulateVertexBuffers() {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  auto& regs = *register_file_;
  assert_not_null(active_vertex_shader_);

  for (const auto& vertex_binding : active_vertex_shader_->vertex_bindings()) {
    int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
            (vertex_binding.fetch_constant / 3) * 6;
    const auto group = reinterpret_cast<xe_gpu_fetch_group_t*>(&regs.values[r]);
    const xe_gpu_vertex_fetch_t* fetch = nullptr;
    switch (vertex_binding.fetch_constant % 3) {
      case 0:
        fetch = &group->vertex_fetch_0;
        break;
      case 1:
        fetch = &group->vertex_fetch_1;
        break;
      case 2:
        fetch = &group->vertex_fetch_2;
        break;
    }
    assert_true(fetch->endian == 2);

    size_t valid_range = size_t(fetch->size * 4);

    trace_writer_.WriteMemoryRead(fetch->address << 2, valid_range);

    auto vertex_shader = static_cast<GL4Shader*>(active_vertex_shader_);
    CircularBuffer::Allocation allocation;
    if (!scratch_buffer_.AcquireCached(fetch->address << 2, valid_range,
                                       &allocation)) {
      // Copy and byte swap the entire buffer.
      // We could be smart about this to save GPU bandwidth by building a CRC
      // as we copy and only if it differs from the previous value committing
      // it (and if it matches just discard and reuse).
      xe::copy_and_swap_32_aligned(
          allocation.host_ptr,
          memory_->TranslatePhysical<const uint32_t*>(fetch->address << 2),
          valid_range / 4);

      // TODO(benvanik): if we could find a way to avoid this, we could use
      // multidraw without flushing.
      glVertexArrayVertexBuffer(
          vertex_shader->vao(),
          static_cast<GLuint>(vertex_binding.binding_index),
          scratch_buffer_.handle(), allocation.offset,
          vertex_binding.stride_words * 4);

      scratch_buffer_.Commit(std::move(allocation));
    } else {
      // TODO(benvanik): if we could find a way to avoid this, we could use
      // multidraw without flushing.
      glVertexArrayVertexBuffer(
          vertex_shader->vao(),
          static_cast<GLuint>(vertex_binding.binding_index),
          scratch_buffer_.handle(), allocation.offset,
          vertex_binding.stride_words * 4);
    }
  }

  return UpdateStatus::kCompatible;
}

GL4CommandProcessor::UpdateStatus GL4CommandProcessor::PopulateSamplers() {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  bool mismatch = false;

  // VS and PS samplers are shared, but may be used exclusively.
  // We walk each and setup lazily.
  bool has_setup_sampler[32] = {false};

  // Vertex texture samplers.
  for (auto& texture_binding : active_vertex_shader_->texture_bindings()) {
    if (has_setup_sampler[texture_binding.fetch_constant]) {
      continue;
    }
    has_setup_sampler[texture_binding.fetch_constant] = true;
    auto status = PopulateSampler(texture_binding);
    if (status == UpdateStatus::kError) {
      return status;
    } else if (status == UpdateStatus::kMismatch) {
      mismatch = true;
    }
  }

  // Pixel shader texture sampler.
  for (auto& texture_binding : active_pixel_shader_->texture_bindings()) {
    if (has_setup_sampler[texture_binding.fetch_constant]) {
      continue;
    }
    has_setup_sampler[texture_binding.fetch_constant] = true;
    auto status = PopulateSampler(texture_binding);
    if (status == UpdateStatus::kError) {
      return UpdateStatus::kError;
    } else if (status == UpdateStatus::kMismatch) {
      mismatch = true;
    }
  }

  return mismatch ? UpdateStatus::kMismatch : UpdateStatus::kCompatible;
}

GL4CommandProcessor::UpdateStatus GL4CommandProcessor::PopulateSampler(
    const Shader::TextureBinding& texture_binding) {
  auto& regs = *register_file_;
  int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
          texture_binding.fetch_constant * 6;
  auto group = reinterpret_cast<const xe_gpu_fetch_group_t*>(&regs.values[r]);
  auto& fetch = group->texture_fetch;

  // Reset slot.
  // If we fail, we still draw but with an invalid texture.
  draw_batcher_.set_texture_sampler(texture_binding.fetch_constant, 0, 0);

  if (FLAGS_disable_textures) {
    return UpdateStatus::kCompatible;
  }

  // ?
  if (!fetch.type) {
    return UpdateStatus::kCompatible;
  }
  assert_true(fetch.type == 0x2);

  TextureInfo texture_info;
  if (!TextureInfo::Prepare(fetch, &texture_info)) {
    XELOGE("Unable to parse texture fetcher info");
    return UpdateStatus::kCompatible;  // invalid texture used
  }
  SamplerInfo sampler_info;
  if (!SamplerInfo::Prepare(fetch, texture_binding.fetch_instr,
                            &sampler_info)) {
    XELOGE("Unable to parse sampler info");
    return UpdateStatus::kCompatible;  // invalid texture used
  }

  trace_writer_.WriteMemoryRead(texture_info.guest_address,
                                texture_info.input_length);

  auto entry_view = texture_cache_.Demand(texture_info, sampler_info);
  if (!entry_view) {
    // Unable to create/fetch/etc.
    XELOGE("Failed to demand texture");
    return UpdateStatus::kCompatible;
  }

  // Shaders will use bindless to fetch right from it.
  draw_batcher_.set_texture_sampler(texture_binding.fetch_constant,
                                    entry_view->texture_sampler_handle,
                                    fetch.swizzle);

  return UpdateStatus::kCompatible;
}

bool GL4CommandProcessor::IssueCopy() {
  SCOPE_profile_cpu_f("gpu");
  auto& regs = *register_file_;

  // This is used to resolve surfaces, taking them from EDRAM render targets
  // to system memory. It can optionally clear color/depth surfaces, too.
  // The command buffer has stuff for actually doing this by drawing, however
  // we should be able to do it without that much easier.

  uint32_t copy_control = regs[XE_GPU_REG_RB_COPY_CONTROL].u32;
  // Render targets 0-3, 4 = depth
  uint32_t copy_src_select = copy_control & 0x7;
  bool color_clear_enabled = (copy_control >> 8) & 0x1;
  bool depth_clear_enabled = (copy_control >> 9) & 0x1;
  auto copy_command = static_cast<CopyCommand>((copy_control >> 20) & 0x3);

  uint32_t copy_dest_info = regs[XE_GPU_REG_RB_COPY_DEST_INFO].u32;
  auto copy_dest_endian = static_cast<Endian128>(copy_dest_info & 0x7);
  uint32_t copy_dest_array = (copy_dest_info >> 3) & 0x1;
  assert_true(copy_dest_array == 0);
  uint32_t copy_dest_slice = (copy_dest_info >> 4) & 0x7;
  assert_true(copy_dest_slice == 0);
  auto copy_dest_format =
      static_cast<ColorFormat>((copy_dest_info >> 7) & 0x3F);
  uint32_t copy_dest_number = (copy_dest_info >> 13) & 0x7;
  // assert_true(copy_dest_number == 0); // ?
  uint32_t copy_dest_bias = (copy_dest_info >> 16) & 0x3F;
  // assert_true(copy_dest_bias == 0);
  uint32_t copy_dest_swap = (copy_dest_info >> 25) & 0x1;

  uint32_t copy_dest_base = regs[XE_GPU_REG_RB_COPY_DEST_BASE].u32;
  uint32_t copy_dest_pitch = regs[XE_GPU_REG_RB_COPY_DEST_PITCH].u32;
  uint32_t copy_dest_height = (copy_dest_pitch >> 16) & 0x3FFF;
  copy_dest_pitch &= 0x3FFF;

  // None of this is supported yet:
  uint32_t copy_surface_slice = regs[XE_GPU_REG_RB_COPY_SURFACE_SLICE].u32;
  assert_true(copy_surface_slice == 0);
  uint32_t copy_func = regs[XE_GPU_REG_RB_COPY_FUNC].u32;
  assert_true(copy_func == 0);
  uint32_t copy_ref = regs[XE_GPU_REG_RB_COPY_REF].u32;
  assert_true(copy_ref == 0);
  uint32_t copy_mask = regs[XE_GPU_REG_RB_COPY_MASK].u32;
  assert_true(copy_mask == 0);

  // RB_SURFACE_INFO
  // http://fossies.org/dox/MesaLib-10.3.5/fd2__gmem_8c_source.html
  uint32_t surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO].u32;
  uint32_t surface_pitch = surface_info & 0x3FFF;
  auto surface_msaa = static_cast<MsaaSamples>((surface_info >> 16) & 0x3);

  // Depending on the source, pick the buffer we'll be sourcing.
  // We then query for a cached framebuffer setup with that buffer active.
  TextureFormat src_format = TextureFormat::kUnknown;
  GLuint color_targets[4] = {kAnyTarget, kAnyTarget, kAnyTarget, kAnyTarget};
  GLuint depth_target = kAnyTarget;
  if (copy_src_select <= 3 || color_clear_enabled) {
    // Source from a color target.
    uint32_t color_info[4] = {
        regs[XE_GPU_REG_RB_COLOR_INFO].u32,
        regs[XE_GPU_REG_RB_COLOR1_INFO].u32,
        regs[XE_GPU_REG_RB_COLOR2_INFO].u32,
        regs[XE_GPU_REG_RB_COLOR3_INFO].u32,
    };
    uint32_t color_base = color_info[copy_src_select] & 0xFFF;
    auto color_format = static_cast<ColorRenderTargetFormat>(
        (color_info[copy_src_select] >> 16) & 0xF);
    color_targets[copy_src_select] = GetColorRenderTarget(
        surface_pitch, surface_msaa, color_base, color_format);

    if (copy_src_select <= 3) {
      src_format = ColorRenderTargetToTextureFormat(color_format);
    }
  }

  // Grab the depth/stencil if we're sourcing from it or clear is enabled.
  if (copy_src_select > 3 || depth_clear_enabled) {
    uint32_t depth_info = regs[XE_GPU_REG_RB_DEPTH_INFO].u32;
    uint32_t depth_base = depth_info & 0xFFF;
    auto depth_format =
        static_cast<DepthRenderTargetFormat>((depth_info >> 16) & 0x1);
    depth_target = GetDepthRenderTarget(surface_pitch, surface_msaa, depth_base,
                                        depth_format);

    if (copy_src_select > 3) {
      src_format = DepthRenderTargetToTextureFormat(depth_format);
    }
  }

  auto source_framebuffer = GetFramebuffer(color_targets, depth_target);
  if (!source_framebuffer) {
    // If we get here we are likely missing some state checks.
    assert_always("No framebuffer for copy source? no-op copy?");
    XELOGE("No framebuffer for copy source");
    return false;
  }

  active_framebuffer_ = source_framebuffer;

  GLenum read_format;
  GLenum read_type;
  size_t read_size = 0;
  switch (copy_dest_format) {
    case ColorFormat::k_1_5_5_5:
      read_format = GL_RGB5_A1;
      read_type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
      read_size = 16;
      break;
    case ColorFormat::k_2_10_10_10:
      read_format = GL_RGB10_A2;
      read_type = GL_UNSIGNED_INT_10_10_10_2;
      read_size = 32;
      break;
    case ColorFormat::k_4_4_4_4:
      read_format = GL_RGBA4;
      read_type = GL_UNSIGNED_SHORT_4_4_4_4;
      read_size = 16;
      break;
    case ColorFormat::k_5_6_5:
      read_format = GL_RGB565;
      read_type = GL_UNSIGNED_SHORT_5_6_5;
      read_size = 16;
      break;
    case ColorFormat::k_8:
      read_format = GL_R8;
      read_type = GL_UNSIGNED_BYTE;
      read_size = 8;
      break;
    case ColorFormat::k_8_8:
      read_format = GL_RG8;
      read_type = GL_UNSIGNED_BYTE;
      read_size = 16;
      break;
    case ColorFormat::k_8_8_8_8:
      read_format = copy_dest_swap ? GL_BGRA : GL_RGBA;
      read_type = GL_UNSIGNED_BYTE;
      read_size = 32;
      break;
    case ColorFormat::k_16:
      read_format = GL_R16;
      read_type = GL_UNSIGNED_SHORT;
      read_size = 16;
      break;
    case ColorFormat::k_16_FLOAT:
      read_format = GL_R16F;
      read_type = GL_HALF_FLOAT;
      read_size = 16;
      break;
    case ColorFormat::k_16_16:
      read_format = GL_RG16;
      read_type = GL_UNSIGNED_SHORT;
      read_size = 32;
      break;
    case ColorFormat::k_16_16_FLOAT:
      read_format = GL_RG16F;
      read_type = GL_HALF_FLOAT;
      read_size = 32;
      break;
    case ColorFormat::k_16_16_16_16:
      read_format = GL_RGBA16;
      read_type = GL_UNSIGNED_SHORT;
      read_size = 64;
      break;
    case ColorFormat::k_16_16_16_16_FLOAT:
      read_format = GL_RGBA16F;
      read_type = GL_HALF_FLOAT;
      read_size = 64;
      break;
    case ColorFormat::k_32_FLOAT:
      read_format = GL_R32F;
      read_type = GL_FLOAT;
      read_size = 32;
      break;
    case ColorFormat::k_32_32_FLOAT:
      read_format = GL_RG32F;
      read_type = GL_FLOAT;
      read_size = 64;
      break;
    case ColorFormat::k_32_32_32_32_FLOAT:
      read_format = GL_RGBA32F;
      read_type = GL_FLOAT;
      read_size = 128;
      break;
    case ColorFormat::k_10_11_11:
    case ColorFormat::k_11_11_10:
      read_format = GL_R11F_G11F_B10F;
      read_type = GL_UNSIGNED_INT_10F_11F_11F_REV;
      read_size = 32;
      break;
    default:
      assert_unhandled_case(copy_dest_format);
      return false;
  }

  // TODO(benvanik): swap channel ordering on copy_dest_swap
  //                 Can we use GL swizzles for this?

  // Swap byte order during read.
  // TODO(benvanik): handle other endian modes.
  switch (copy_dest_endian) {
    case Endian128::kUnspecified:
      glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE);
      break;
    case Endian128::k8in32:
      glPixelStorei(GL_PACK_SWAP_BYTES, GL_TRUE);
      break;
    default:
      // assert_unhandled_case(copy_dest_endian);
      glPixelStorei(GL_PACK_SWAP_BYTES, GL_TRUE);
      break;
  }

  // TODO(benvanik): tweak alignments/strides.
  // glPixelStorei(GL_PACK_ALIGNMENT, 1);
  // glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  // glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);

  // TODO(benvanik): any way to scissor this? a200 has:
  // REG_A2XX_RB_COPY_DEST_OFFSET = A2XX_RB_COPY_DEST_OFFSET_X(tile->xoff) |
  //                                A2XX_RB_COPY_DEST_OFFSET_Y(tile->yoff);
  // but I can't seem to find something similar.
  uint32_t dest_logical_width = copy_dest_pitch;
  uint32_t dest_logical_height = copy_dest_height;
  uint32_t dest_block_width = xe::round_up(dest_logical_width, 32);
  uint32_t dest_block_height = /*xe::round_up(*/ dest_logical_height /*, 32)*/;

  uint32_t window_offset = regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET].u32;
  int16_t window_offset_x = window_offset & 0x7FFF;
  int16_t window_offset_y = (window_offset >> 16) & 0x7FFF;
  if (window_offset_x & 0x4000) {
    window_offset_x |= 0x8000;
  }
  if (window_offset_y & 0x4000) {
    window_offset_y |= 0x8000;
  }

  // HACK: vertices to use are always in vf0.
  int copy_vertex_fetch_slot = 0;
  int r =
      XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + (copy_vertex_fetch_slot / 3) * 6;
  const auto group = reinterpret_cast<xe_gpu_fetch_group_t*>(&regs.values[r]);
  const xe_gpu_vertex_fetch_t* fetch = nullptr;
  switch (copy_vertex_fetch_slot % 3) {
    case 0:
      fetch = &group->vertex_fetch_0;
      break;
    case 1:
      fetch = &group->vertex_fetch_1;
      break;
    case 2:
      fetch = &group->vertex_fetch_2;
      break;
  }
  assert_true(fetch->type == 3);
  assert_true(fetch->endian == 2);
  assert_true(fetch->size == 6);
  const uint8_t* vertex_addr = memory_->TranslatePhysical(fetch->address << 2);
  trace_writer_.WriteMemoryRead(fetch->address << 2, fetch->size * 4);
  int32_t dest_min_x = int32_t((std::min(
      std::min(
          GpuSwap(xe::load<float>(vertex_addr + 0), Endian(fetch->endian)),
          GpuSwap(xe::load<float>(vertex_addr + 8), Endian(fetch->endian))),
      GpuSwap(xe::load<float>(vertex_addr + 16), Endian(fetch->endian)))));
  int32_t dest_max_x = int32_t((std::max(
      std::max(
          GpuSwap(xe::load<float>(vertex_addr + 0), Endian(fetch->endian)),
          GpuSwap(xe::load<float>(vertex_addr + 8), Endian(fetch->endian))),
      GpuSwap(xe::load<float>(vertex_addr + 16), Endian(fetch->endian)))));
  int32_t dest_min_y = int32_t((std::min(
      std::min(
          GpuSwap(xe::load<float>(vertex_addr + 4), Endian(fetch->endian)),
          GpuSwap(xe::load<float>(vertex_addr + 12), Endian(fetch->endian))),
      GpuSwap(xe::load<float>(vertex_addr + 20), Endian(fetch->endian)))));
  int32_t dest_max_y = int32_t((std::max(
      std::max(
          GpuSwap(xe::load<float>(vertex_addr + 4), Endian(fetch->endian)),
          GpuSwap(xe::load<float>(vertex_addr + 12), Endian(fetch->endian))),
      GpuSwap(xe::load<float>(vertex_addr + 20), Endian(fetch->endian)))));
  Rect2D dest_rect(dest_min_x, dest_min_y, dest_max_x - dest_min_x,
                   dest_max_y - dest_min_y);
  Rect2D src_rect(0, 0, dest_rect.width, dest_rect.height);

  // The dest base address passed in has already been offset by the window
  // offset, so to ensure texture lookup works we need to offset it.
  // TODO(benvanik): allow texture cache to lookup partial textures.
  // TODO(benvanik): change based on format.
  int32_t dest_offset = window_offset_y * copy_dest_pitch * int(read_size / 8);
  dest_offset += window_offset_x * 32 * int(read_size / 8);
  copy_dest_base += dest_offset;

  // Destination pointer in guest memory.
  // We have GL throw bytes directly into it.
  // TODO(benvanik): copy to staging texture then PBO back?
  void* ptr = memory_->TranslatePhysical(copy_dest_base);
  size_t size = copy_dest_pitch * copy_dest_height * (read_size / 8);

  auto blitter = static_cast<xe::ui::gl::GLContext*>(context_.get())->blitter();

  // Make active so glReadPixels reads from us.
  switch (copy_command) {
    case CopyCommand::kRaw: {
      // This performs a byte-for-byte copy of the textures from src to dest
      // with no conversion. Byte swapping may still occur.
      if (copy_src_select <= 3) {
        // Source from a bound render target.
        // TODO(benvanik): RAW copy.
        last_framebuffer_texture_ = texture_cache_.CopyTexture(
            blitter, copy_dest_base, dest_logical_width, dest_logical_height,
            dest_block_width, dest_block_height,
            ColorFormatToTextureFormat(copy_dest_format),
            copy_dest_swap ? true : false, color_targets[copy_src_select],
            src_rect, dest_rect);
        if (!FLAGS_disable_framebuffer_readback) {
          // std::memset(ptr, 0xDE,
          //             copy_dest_pitch * copy_dest_height * (read_size / 8));
          // glReadPixels(0, 0, copy_dest_pitch, copy_dest_height, read_format,
          //              read_type, ptr);
        }
      } else {
        // Source from the bound depth/stencil target.
        // TODO(benvanik): RAW copy.
        texture_cache_.CopyTexture(
            blitter, copy_dest_base, dest_logical_width, dest_logical_height,
            dest_block_width, dest_block_height, src_format,
            copy_dest_swap ? true : false, depth_target, src_rect, dest_rect);
        if (!FLAGS_disable_framebuffer_readback) {
          // std::memset(ptr, 0xDE,
          //             copy_dest_pitch * copy_dest_height * (read_size / 8));
          // glReadPixels(0, 0, copy_dest_pitch, copy_dest_height,
          //              GL_DEPTH_STENCIL, read_type, ptr);
        }
      }
      break;
    }
    case CopyCommand::kConvert: {
      if (copy_src_select <= 3) {
        // Source from a bound render target.
        // Either copy the readbuffer into an existing texture or create a new
        // one in the cache so we can service future upload requests.
        last_framebuffer_texture_ = texture_cache_.ConvertTexture(
            blitter, copy_dest_base, dest_logical_width, dest_logical_height,
            dest_block_width, dest_block_height,
            ColorFormatToTextureFormat(copy_dest_format),
            copy_dest_swap ? true : false, color_targets[copy_src_select],
            src_rect, dest_rect);
        if (!FLAGS_disable_framebuffer_readback) {
          // std::memset(ptr, 0xDE,
          //             copy_dest_pitch * copy_dest_height * (read_size / 8));
          // glReadPixels(0, 0, copy_dest_pitch, copy_dest_height, read_format,
          //              read_type, ptr);
        }
      } else {
        // Source from the bound depth/stencil target.
        texture_cache_.ConvertTexture(
            blitter, copy_dest_base, dest_logical_width, dest_logical_height,
            dest_block_width, dest_block_height, src_format,
            copy_dest_swap ? true : false, depth_target, src_rect, dest_rect);
        if (!FLAGS_disable_framebuffer_readback) {
          // std::memset(ptr, 0xDE,
          //             copy_dest_pitch * copy_dest_height * (read_size / 8));
          // glReadPixels(0, 0, copy_dest_pitch, copy_dest_height,
          //              GL_DEPTH_STENCIL, read_type, ptr);
        }
      }
      break;
    }
    case CopyCommand::kConstantOne:
    case CopyCommand::kNull:
    default:
      // assert_unhandled_case(copy_command);
      return false;
  }

  // Perform any requested clears.
  uint32_t copy_depth_clear = regs[XE_GPU_REG_RB_DEPTH_CLEAR].u32;
  uint32_t copy_color_clear = regs[XE_GPU_REG_RB_COLOR_CLEAR].u32;
  uint32_t copy_color_clear_low = regs[XE_GPU_REG_RB_COLOR_CLEAR_LOW].u32;
  assert_true(copy_color_clear == copy_color_clear_low);

  if (color_clear_enabled) {
    // Clear the render target we selected for copy.
    assert_true(copy_src_select < 3);
    // TODO(benvanik): verify color order.
    float color[] = {(copy_color_clear & 0xFF) / 255.0f,
                     ((copy_color_clear >> 8) & 0xFF) / 255.0f,
                     ((copy_color_clear >> 16) & 0xFF) / 255.0f,
                     ((copy_color_clear >> 24) & 0xFF) / 255.0f};
    // TODO(benvanik): remove query.
    GLboolean old_color_mask[4];
    glGetBooleani_v(GL_COLOR_WRITEMASK, copy_src_select, old_color_mask);
    glColorMaski(copy_src_select, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearNamedFramebufferfv(source_framebuffer->framebuffer, GL_COLOR,
                              copy_src_select, color);
    glColorMaski(copy_src_select, old_color_mask[0], old_color_mask[1],
                 old_color_mask[2], old_color_mask[3]);
  }

  if (depth_clear_enabled && depth_target != kAnyTarget) {
    // Clear the current depth buffer.
    // TODO(benvanik): verify format.
    GLfloat depth = {(copy_depth_clear & 0xFFFFFF00) /
                     static_cast<float>(0xFFFFFF00)};
    GLint stencil = copy_depth_clear & 0xFF;
    GLint old_draw_framebuffer;
    GLboolean old_depth_mask;
    GLint old_stencil_mask;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_framebuffer);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &old_depth_mask);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &old_stencil_mask);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);
    // HACK: this should work, but throws INVALID_ENUM on nvidia drivers.
    // GLEW signature differs from OpenGL docs?
    // glClearNamedFramebufferfi(source_framebuffer->framebuffer,
    //                           GL_DEPTH_STENCIL, depth, stencil);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, source_framebuffer->framebuffer);
    glClearBufferfi(GL_DEPTH_STENCIL, 0, depth, stencil);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_draw_framebuffer);
    glDepthMask(old_depth_mask);
    glStencilMask(old_stencil_mask);
  }

  return true;
}

GLuint GL4CommandProcessor::GetColorRenderTarget(
    uint32_t pitch, MsaaSamples samples, uint32_t base,
    ColorRenderTargetFormat format) {
  // Because we don't know the height of anything, we allocate at full res.
  // At 2560x2560, it's impossible for EDRAM to fit anymore.
  uint32_t width = 2560;
  uint32_t height = 2560;

  // NOTE: we strip gamma formats down to normal ones.
  if (format == ColorRenderTargetFormat::k_8_8_8_8_GAMMA) {
    format = ColorRenderTargetFormat::k_8_8_8_8;
  }

  GLenum internal_format;
  switch (format) {
    case ColorRenderTargetFormat::k_8_8_8_8:
    case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      internal_format = GL_RGBA8;
      break;
    case ColorRenderTargetFormat::k_2_10_10_10:
    case ColorRenderTargetFormat::k_2_10_10_10_unknown:
      internal_format = GL_RGB10_A2UI;
      break;
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_unknown:
      internal_format = GL_RGB10_A2;
      break;
    case ColorRenderTargetFormat::k_16_16:
      internal_format = GL_RG16;
      break;
    case ColorRenderTargetFormat::k_16_16_FLOAT:
      internal_format = GL_RG16F;
      break;
    case ColorRenderTargetFormat::k_16_16_16_16:
      internal_format = GL_RGBA16;
      break;
    case ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      internal_format = GL_RGBA16F;
      break;
    case ColorRenderTargetFormat::k_32_FLOAT:
      internal_format = GL_R32F;
      break;
    case ColorRenderTargetFormat::k_32_32_FLOAT:
      internal_format = GL_RG32F;
      break;
    default:
      assert_unhandled_case(format);
      return 0;
  }

  for (auto it = cached_color_render_targets_.begin();
       it != cached_color_render_targets_.end(); ++it) {
    if (it->base == base && it->width == width && it->height == height &&
        it->internal_format == internal_format) {
      return it->texture;
    }
  }
  cached_color_render_targets_.push_back(CachedColorRenderTarget());
  auto cached = &cached_color_render_targets_.back();
  cached->base = base;
  cached->width = width;
  cached->height = height;
  cached->format = format;
  cached->internal_format = internal_format;

  glCreateTextures(GL_TEXTURE_2D, 1, &cached->texture);
  glTextureStorage2D(cached->texture, 1, internal_format, width, height);

  return cached->texture;
}

GLuint GL4CommandProcessor::GetDepthRenderTarget(
    uint32_t pitch, MsaaSamples samples, uint32_t base,
    DepthRenderTargetFormat format) {
  uint32_t width = 2560;
  uint32_t height = 2560;

  GLenum internal_format;
  switch (format) {
    case DepthRenderTargetFormat::kD24S8:
      internal_format = GL_DEPTH24_STENCIL8;
      break;
    case DepthRenderTargetFormat::kD24FS8:
      // TODO(benvanik): not supported in GL?
      internal_format = GL_DEPTH24_STENCIL8;
      break;
    default:
      assert_unhandled_case(format);
      return 0;
  }

  for (auto it = cached_depth_render_targets_.begin();
       it != cached_depth_render_targets_.end(); ++it) {
    if (it->base == base && it->width == width && it->height == height &&
        it->format == format) {
      return it->texture;
    }
  }
  cached_depth_render_targets_.push_back(CachedDepthRenderTarget());
  auto cached = &cached_depth_render_targets_.back();
  cached->base = base;
  cached->width = width;
  cached->height = height;
  cached->format = format;
  cached->internal_format = internal_format;

  glCreateTextures(GL_TEXTURE_2D, 1, &cached->texture);
  glTextureStorage2D(cached->texture, 1, internal_format, width, height);

  return cached->texture;
}

GL4CommandProcessor::CachedFramebuffer* GL4CommandProcessor::GetFramebuffer(
    GLuint color_targets[4], GLuint depth_target) {
  for (auto it = cached_framebuffers_.begin(); it != cached_framebuffers_.end();
       ++it) {
    if ((depth_target == kAnyTarget || it->depth_target == depth_target) &&
        (color_targets[0] == kAnyTarget ||
         it->color_targets[0] == color_targets[0]) &&
        (color_targets[1] == kAnyTarget ||
         it->color_targets[1] == color_targets[1]) &&
        (color_targets[2] == kAnyTarget ||
         it->color_targets[2] == color_targets[2]) &&
        (color_targets[3] == kAnyTarget ||
         it->color_targets[3] == color_targets[3])) {
      return &*it;
    }
  }

  GLuint real_color_targets[4];
  bool any_set = false;
  for (int i = 0; i < 4; ++i) {
    if (color_targets[i] == kAnyTarget) {
      real_color_targets[i] = 0;
    } else {
      any_set = true;
      real_color_targets[i] = color_targets[i];
    }
  }
  GLuint real_depth_target;
  if (depth_target == kAnyTarget) {
    real_depth_target = 0;
  } else {
    any_set = true;
    real_depth_target = depth_target;
  }
  if (!any_set) {
    // No framebuffer required.
    return nullptr;
  }

  cached_framebuffers_.push_back(CachedFramebuffer());
  auto cached = &cached_framebuffers_.back();
  glCreateFramebuffers(1, &cached->framebuffer);
  for (int i = 0; i < 4; ++i) {
    cached->color_targets[i] = real_color_targets[i];
    glNamedFramebufferTexture(cached->framebuffer, GL_COLOR_ATTACHMENT0 + i,
                              real_color_targets[i], 0);
  }
  cached->depth_target = real_depth_target;
  glNamedFramebufferTexture(cached->framebuffer, GL_DEPTH_STENCIL_ATTACHMENT,
                            real_depth_target, 0);

  return cached;
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
