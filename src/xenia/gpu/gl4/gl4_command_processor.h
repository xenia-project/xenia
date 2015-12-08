/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GL4_COMMAND_PROCESSOR_H_
#define XENIA_GPU_GL4_COMMAND_PROCESSOR_H_

#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/threading.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/gl4/draw_batcher.h"
#include "xenia/gpu/gl4/gl4_shader.h"
#include "xenia/gpu/gl4/texture_cache.h"
#include "xenia/gpu/glsl_shader_translator.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/xenos.h"
#include "xenia/kernel/xthread.h"
#include "xenia/memory.h"
#include "xenia/ui/gl/circular_buffer.h"
#include "xenia/ui/gl/gl_context.h"

namespace xe {
namespace gpu {
namespace gl4 {

class GL4GraphicsSystem;

class GL4CommandProcessor : public CommandProcessor {
 public:
  GL4CommandProcessor(GL4GraphicsSystem* graphics_system,
                      kernel::KernelState* kernel_state);
  ~GL4CommandProcessor() override;

  void ClearCaches() override;

  // HACK: for debugging; would be good to have this in a base type.
  TextureCache* texture_cache() { return &texture_cache_; }

  GLuint GetColorRenderTarget(uint32_t pitch, MsaaSamples samples,
                              uint32_t base, ColorRenderTargetFormat format);
  GLuint GetDepthRenderTarget(uint32_t pitch, MsaaSamples samples,
                              uint32_t base, DepthRenderTargetFormat format);

 private:
  enum class UpdateStatus {
    kCompatible,
    kMismatch,
    kError,
  };

  struct CachedFramebuffer {
    GLuint color_targets[4];
    GLuint depth_target;
    GLuint framebuffer;
  };
  struct CachedColorRenderTarget {
    uint32_t base;
    uint32_t width;
    uint32_t height;
    ColorRenderTargetFormat format;
    GLuint texture;
  };
  struct CachedDepthRenderTarget {
    uint32_t base;
    uint32_t width;
    uint32_t height;
    DepthRenderTargetFormat format;
    GLuint texture;
  };
  struct CachedPipeline {
    CachedPipeline();
    ~CachedPipeline();
    GLuint vertex_program;
    GLuint fragment_program;
    struct {
      GLuint default_pipeline;
      GLuint point_list_pipeline;
      GLuint rect_list_pipeline;
      GLuint quad_list_pipeline;
      GLuint line_quad_list_pipeline;
      // TODO(benvanik): others with geometry shaders.
    } handles;
  };

  bool SetupContext() override;
  void ShutdownContext() override;
  GLuint CreateGeometryProgram(const std::string& source);

  void MakeCoherent() override;
  void PrepareForWait() override;
  void ReturnFromWait() override;

  void PerformSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                   uint32_t frontbuffer_height) override;

  Shader* LoadShader(ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(PrimitiveType prim_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info) override;
  UpdateStatus UpdateShaders(PrimitiveType prim_type);
  UpdateStatus UpdateRenderTargets();
  UpdateStatus UpdateState();
  UpdateStatus UpdateViewportState();
  UpdateStatus UpdateRasterizerState();
  UpdateStatus UpdateBlendState();
  UpdateStatus UpdateDepthStencilState();
  UpdateStatus PopulateIndexBuffer(IndexBufferInfo* index_buffer_info);
  UpdateStatus PopulateVertexBuffers();
  UpdateStatus PopulateSamplers();
  UpdateStatus PopulateSampler(const Shader::TextureBinding& texture_binding);
  bool IssueCopy() override;

  CachedFramebuffer* GetFramebuffer(GLuint color_targets[4],
                                    GLuint depth_target);

  GlslShaderTranslator shader_translator_;
  std::vector<std::unique_ptr<GL4Shader>> all_shaders_;
  std::unordered_map<uint64_t, GL4Shader*> shader_cache_;
  CachedFramebuffer* active_framebuffer_ = nullptr;
  GLuint last_framebuffer_texture_ = 0;

  std::vector<CachedFramebuffer> cached_framebuffers_;
  std::vector<CachedColorRenderTarget> cached_color_render_targets_;
  std::vector<CachedDepthRenderTarget> cached_depth_render_targets_;
  std::vector<std::unique_ptr<CachedPipeline>> all_pipelines_;
  std::unordered_map<uint64_t, CachedPipeline*> cached_pipelines_;
  GLuint point_list_geometry_program_ = 0;
  GLuint rect_list_geometry_program_ = 0;
  GLuint quad_list_geometry_program_ = 0;
  GLuint line_quad_list_geometry_program_ = 0;

  TextureCache texture_cache_;

  DrawBatcher draw_batcher_;
  xe::ui::gl::CircularBuffer scratch_buffer_;

 private:
  bool SetShadowRegister(uint32_t* dest, uint32_t register_name);
  bool SetShadowRegister(float* dest, uint32_t register_name);
  struct UpdateRenderTargetsRegisters {
    uint32_t rb_modecontrol;
    uint32_t rb_surface_info;
    uint32_t rb_color_info;
    uint32_t rb_color1_info;
    uint32_t rb_color2_info;
    uint32_t rb_color3_info;
    uint32_t rb_color_mask;
    uint32_t rb_depthcontrol;
    uint32_t rb_stencilrefmask;
    uint32_t rb_depth_info;

    UpdateRenderTargetsRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_render_targets_regs_;
  struct UpdateViewportStateRegisters {
    // uint32_t pa_cl_clip_cntl;
    uint32_t rb_surface_info;
    uint32_t pa_cl_vte_cntl;
    uint32_t pa_su_sc_mode_cntl;
    uint32_t pa_sc_window_offset;
    uint32_t pa_sc_window_scissor_tl;
    uint32_t pa_sc_window_scissor_br;
    float pa_cl_vport_xoffset;
    float pa_cl_vport_yoffset;
    float pa_cl_vport_zoffset;
    float pa_cl_vport_xscale;
    float pa_cl_vport_yscale;
    float pa_cl_vport_zscale;

    UpdateViewportStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_viewport_state_regs_;
  struct UpdateRasterizerStateRegisters {
    uint32_t pa_su_sc_mode_cntl;
    uint32_t pa_sc_screen_scissor_tl;
    uint32_t pa_sc_screen_scissor_br;
    uint32_t multi_prim_ib_reset_index;

    UpdateRasterizerStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_rasterizer_state_regs_;
  struct UpdateBlendStateRegisters {
    uint32_t rb_blendcontrol[4];
    float rb_blend_rgba[4];

    UpdateBlendStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_blend_state_regs_;
  struct UpdateDepthStencilStateRegisters {
    uint32_t rb_depthcontrol;
    uint32_t rb_stencilrefmask;

    UpdateDepthStencilStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_depth_stencil_state_regs_;
  struct UpdateShadersRegisters {
    PrimitiveType prim_type;
    uint32_t pa_su_sc_mode_cntl;
    uint32_t sq_program_cntl;
    uint32_t sq_context_misc;
    GL4Shader* vertex_shader;
    GL4Shader* pixel_shader;

    UpdateShadersRegisters() { Reset(); }
    void Reset() {
      sq_program_cntl = 0;
      vertex_shader = pixel_shader = nullptr;
    }
  } update_shaders_regs_;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_COMMAND_PROCESSOR_H_
