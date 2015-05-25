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
#include <functional>
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>

#include "xenia/gpu/gl4/circular_buffer.h"
#include "xenia/gpu/gl4/draw_batcher.h"
#include "xenia/gpu/gl4/gl_context.h"
#include "xenia/gpu/gl4/gl4_shader.h"
#include "xenia/gpu/gl4/texture_cache.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/tracing.h"
#include "xenia/gpu/xenos.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/memory.h"

namespace xe {
namespace kernel {
class XHostThread;
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace gpu {
namespace gl4 {

class GL4GraphicsSystem;

struct SwapParameters {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;

  GLuint framebuffer_texture;
};

enum class SwapMode {
  kNormal,
  kIgnored,
};

class CommandProcessor {
 public:
  CommandProcessor(GL4GraphicsSystem* graphics_system);
  ~CommandProcessor();

  typedef std::function<void(const SwapParameters& params)> SwapHandler;
  void set_swap_handler(SwapHandler fn) { swap_handler_ = fn; }

  uint64_t QueryTime();
  uint32_t counter() const { return counter_; }
  void increment_counter() { counter_++; }

  bool Initialize(std::unique_ptr<GLContext> context);
  void Shutdown();
  void CallInThread(std::function<void()> fn);

  void set_swap_mode(SwapMode swap_mode) { swap_mode_ = swap_mode; }
  void IssueSwap();
  void IssueSwap(uint32_t frontbuffer_width, uint32_t frontbuffer_height);

  void RequestFrameTrace(const std::wstring& root_path);
  void BeginTracing(const std::wstring& root_path);
  void EndTracing();

  void InitializeRingBuffer(uint32_t ptr, uint32_t page_count);
  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size);

  void UpdateWritePointer(uint32_t value);

  void ExecutePacket(uint32_t ptr, uint32_t count);

  // HACK: for debugging; would be good to have this in a base type.
  TextureCache* texture_cache() { return &texture_cache_; }
  GL4Shader* active_vertex_shader() const { return active_vertex_shader_; }
  GL4Shader* active_pixel_shader() const { return active_pixel_shader_; }

  GLuint GetColorRenderTarget(uint32_t pitch, xenos::MsaaSamples samples,
                              uint32_t base,
                              xenos::ColorRenderTargetFormat format);
  GLuint GetDepthRenderTarget(uint32_t pitch, xenos::MsaaSamples samples,
                              uint32_t base,
                              xenos::DepthRenderTargetFormat format);

 private:
  class RingbufferReader;

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
    xenos::ColorRenderTargetFormat format;
    GLuint texture;
  };
  struct CachedDepthRenderTarget {
    uint32_t base;
    uint32_t width;
    uint32_t height;
    xenos::DepthRenderTargetFormat format;
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

  void WorkerThreadMain();
  bool SetupGL();
  void ShutdownGL();
  GLuint CreateGeometryProgram(const std::string& source);

  void WriteRegister(uint32_t index, uint32_t value);
  void MakeCoherent();
  void PrepareForWait();
  void ReturnFromWait();

  void ExecutePrimaryBuffer(uint32_t start_index, uint32_t end_index);
  void ExecuteIndirectBuffer(uint32_t ptr, uint32_t length);
  bool ExecutePacket(RingbufferReader* reader);
  bool ExecutePacketType0(RingbufferReader* reader, uint32_t packet);
  bool ExecutePacketType1(RingbufferReader* reader, uint32_t packet);
  bool ExecutePacketType2(RingbufferReader* reader, uint32_t packet);
  bool ExecutePacketType3(RingbufferReader* reader, uint32_t packet);
  bool ExecutePacketType3_ME_INIT(RingbufferReader* reader, uint32_t packet,
                                  uint32_t count);
  bool ExecutePacketType3_NOP(RingbufferReader* reader, uint32_t packet,
                              uint32_t count);
  bool ExecutePacketType3_INTERRUPT(RingbufferReader* reader, uint32_t packet,
                                    uint32_t count);
  bool ExecutePacketType3_XE_SWAP(RingbufferReader* reader, uint32_t packet,
                                  uint32_t count);
  bool ExecutePacketType3_INDIRECT_BUFFER(RingbufferReader* reader,
                                          uint32_t packet, uint32_t count);
  bool ExecutePacketType3_WAIT_REG_MEM(RingbufferReader* reader,
                                       uint32_t packet, uint32_t count);
  bool ExecutePacketType3_REG_RMW(RingbufferReader* reader, uint32_t packet,
                                  uint32_t count);
  bool ExecutePacketType3_COND_WRITE(RingbufferReader* reader, uint32_t packet,
                                     uint32_t count);
  bool ExecutePacketType3_EVENT_WRITE(RingbufferReader* reader, uint32_t packet,
                                      uint32_t count);
  bool ExecutePacketType3_EVENT_WRITE_SHD(RingbufferReader* reader,
                                          uint32_t packet, uint32_t count);
  bool ExecutePacketType3_EVENT_WRITE_EXT(RingbufferReader* reader,
                                          uint32_t packet, uint32_t count);
  bool ExecutePacketType3_DRAW_INDX(RingbufferReader* reader, uint32_t packet,
                                    uint32_t count);
  bool ExecutePacketType3_DRAW_INDX_2(RingbufferReader* reader, uint32_t packet,
                                      uint32_t count);
  bool ExecutePacketType3_SET_CONSTANT(RingbufferReader* reader,
                                       uint32_t packet, uint32_t count);
  bool ExecutePacketType3_SET_CONSTANT2(RingbufferReader* reader,
                                        uint32_t packet, uint32_t count);
  bool ExecutePacketType3_LOAD_ALU_CONSTANT(RingbufferReader* reader,
                                            uint32_t packet, uint32_t count);
  bool ExecutePacketType3_SET_SHADER_CONSTANTS(RingbufferReader* reader,
                                               uint32_t packet, uint32_t count);
  bool ExecutePacketType3_IM_LOAD(RingbufferReader* reader, uint32_t packet,
                                  uint32_t count);
  bool ExecutePacketType3_IM_LOAD_IMMEDIATE(RingbufferReader* reader,

                                            uint32_t packet, uint32_t count);
  bool ExecutePacketType3_INVALIDATE_STATE(RingbufferReader* reader,
                                           uint32_t packet, uint32_t count);

  bool LoadShader(ShaderType shader_type, uint32_t guest_address,
                  const uint32_t* host_address, uint32_t dword_count);

  bool IssueDraw();
  UpdateStatus UpdateShaders(PrimitiveType prim_type);
  UpdateStatus UpdateRenderTargets();
  UpdateStatus UpdateState();
  UpdateStatus UpdateViewportState();
  UpdateStatus UpdateRasterizerState();
  UpdateStatus UpdateBlendState();
  UpdateStatus UpdateDepthStencilState();
  UpdateStatus PopulateIndexBuffer();
  UpdateStatus PopulateVertexBuffers();
  UpdateStatus PopulateSamplers();
  UpdateStatus PopulateSampler(const Shader::SamplerDesc& desc);
  bool IssueCopy();

  CachedFramebuffer* GetFramebuffer(GLuint color_targets[4],
                                    GLuint depth_target);

  Memory* memory_;
  GL4GraphicsSystem* graphics_system_;
  RegisterFile* register_file_;

  TraceWriter trace_writer_;
  enum class TraceState {
    kDisabled,
    kStreaming,
    kSingleFrame,
  };
  TraceState trace_state_;
  std::wstring trace_frame_path_;

  std::atomic<bool> worker_running_;
  kernel::object_ref<kernel::XHostThread> worker_thread_;

  std::unique_ptr<GLContext> context_;
  SwapHandler swap_handler_;
  std::queue<std::function<void()>> pending_fns_;

  SwapMode swap_mode_;

  uint64_t time_base_;
  uint32_t counter_;

  uint32_t primary_buffer_ptr_;
  uint32_t primary_buffer_size_;

  uint32_t read_ptr_index_;
  uint32_t read_ptr_update_freq_;
  uint32_t read_ptr_writeback_ptr_;

  HANDLE write_ptr_index_event_;
  std::atomic<uint32_t> write_ptr_index_;

  uint64_t bin_select_;
  uint64_t bin_mask_;

  bool has_bindless_vbos_;

  std::vector<std::unique_ptr<GL4Shader>> all_shaders_;
  std::unordered_map<uint64_t, GL4Shader*> shader_cache_;
  GL4Shader* active_vertex_shader_;
  GL4Shader* active_pixel_shader_;
  CachedFramebuffer* active_framebuffer_;
  GLuint last_framebuffer_texture_;
  uint32_t last_swap_width_;
  uint32_t last_swap_height_;

  std::vector<CachedFramebuffer> cached_framebuffers_;
  std::vector<CachedColorRenderTarget> cached_color_render_targets_;
  std::vector<CachedDepthRenderTarget> cached_depth_render_targets_;
  std::vector<std::unique_ptr<CachedPipeline>> all_pipelines_;
  std::unordered_map<uint64_t, CachedPipeline*> cached_pipelines_;
  GLuint point_list_geometry_program_;
  GLuint rect_list_geometry_program_;
  GLuint quad_list_geometry_program_;
  GLuint line_quad_list_geometry_program_;
  struct {
    xenos::IndexFormat format;
    xenos::Endian endianness;
    uint32_t count;
    uint32_t guest_base;
    size_t length;
  } index_buffer_info_;
  uint32_t draw_index_count_;

  TextureCache texture_cache_;

  DrawBatcher draw_batcher_;
  CircularBuffer scratch_buffer_;

 private:
  bool SetShadowRegister(uint32_t& dest, uint32_t register_name);
  bool SetShadowRegister(float& dest, uint32_t register_name);
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
