/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_PIPELINE_CACHE_H_
#define XENIA_GPU_VULKAN_PIPELINE_CACHE_H_

#include <unordered_map>

#include "third_party/xxhash/xxhash.h"

#include "xenia/gpu/glsl_shader_translator.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/vulkan/render_cache.h"
#include "xenia/gpu/vulkan/vulkan_shader.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/spirv/spirv_disassembler.h"
#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace gpu {
namespace vulkan {

// Configures and caches pipelines based on render state.
// This is responsible for properly setting all state required for a draw
// including shaders, various blend/etc options, and input configuration.
class PipelineCache {
 public:
  enum class UpdateStatus {
    kCompatible,
    kMismatch,
    kError,
  };

  PipelineCache(RegisterFile* register_file, ui::vulkan::VulkanDevice* device);
  ~PipelineCache();

  VkResult Initialize(VkDescriptorSetLayout uniform_descriptor_set_layout,
                      VkDescriptorSetLayout texture_descriptor_set_layout,
                      VkDescriptorSetLayout vertex_descriptor_set_layout);
  void Shutdown();

  // Loads a shader from the cache, possibly translating it.
  VulkanShader* LoadShader(ShaderType shader_type, uint32_t guest_address,
                           const uint32_t* host_address, uint32_t dword_count);

  // Configures a pipeline using the current render state and the given render
  // pass. If a previously available pipeline is available it will be used,
  // otherwise a new one may be created. Any state that can be set dynamically
  // in the command buffer is issued at this time.
  // Returns whether the pipeline could be successfully created.
  UpdateStatus ConfigurePipeline(VkCommandBuffer command_buffer,
                                 const RenderState* render_state,
                                 VulkanShader* vertex_shader,
                                 VulkanShader* pixel_shader,
                                 PrimitiveType primitive_type,
                                 VkPipeline* pipeline_out);

  // Sets required dynamic state on the command buffer.
  // Only state that has changed since the last call will be set unless
  // full_update is true.
  bool SetDynamicState(VkCommandBuffer command_buffer, bool full_update);

  // Pipeline layout shared by all pipelines.
  VkPipelineLayout pipeline_layout() const { return pipeline_layout_; }

  // Clears all cached content.
  void ClearCache();

 private:
  // Creates or retrieves an existing pipeline for the currently configured
  // state.
  VkPipeline GetPipeline(const RenderState* render_state, uint64_t hash_key);

  bool TranslateShader(VulkanShader* shader, xenos::xe_gpu_program_cntl_t cntl);

  void DumpShaderDisasmAMD(VkPipeline pipeline);
  void DumpShaderDisasmNV(const VkGraphicsPipelineCreateInfo& info);

  // Gets a geometry shader used to emulate the given primitive type.
  // Returns nullptr if the primitive doesn't need to be emulated.
  VkShaderModule GetGeometryShader(PrimitiveType primitive_type,
                                   bool is_line_mode);

  RegisterFile* register_file_ = nullptr;
  ui::vulkan::VulkanDevice* device_ = nullptr;

  // Reusable shader translator.
  std::unique_ptr<ShaderTranslator> shader_translator_ = nullptr;
  // Disassembler used to get the SPIRV disasm. Only used in debug.
  xe::ui::spirv::SpirvDisassembler disassembler_;
  // All loaded shaders mapped by their guest hash key.
  std::unordered_map<uint64_t, VulkanShader*> shader_map_;

  // Vulkan pipeline cache, which in theory helps us out.
  // This can be serialized to disk and reused, if we want.
  VkPipelineCache pipeline_cache_ = nullptr;
  // Layout used for all pipelines describing our uniforms, textures, and push
  // constants.
  VkPipelineLayout pipeline_layout_ = nullptr;

  // Shared geometry shaders.
  struct {
    VkShaderModule line_quad_list;
    VkShaderModule point_list;
    VkShaderModule quad_list;
    VkShaderModule rect_list;
  } geometry_shaders_;

  // Shared dummy pixel shader.
  VkShaderModule dummy_pixel_shader_;

  // Hash state used to incrementally produce pipeline hashes during update.
  // By the time the full update pass has run the hash will represent the
  // current state in a way that can uniquely identify the produced VkPipeline.
  XXH64_state_t hash_state_;
  // All previously generated pipelines mapped by hash.
  std::unordered_map<uint64_t, VkPipeline> cached_pipelines_;

  // Previously used pipeline. This matches our current state settings
  // and allows us to quickly(ish) reuse the pipeline if no registers have
  // changed.
  VkPipeline current_pipeline_ = nullptr;

 private:
  UpdateStatus UpdateState(VulkanShader* vertex_shader,
                           VulkanShader* pixel_shader,
                           PrimitiveType primitive_type);

  UpdateStatus UpdateRenderTargetState();
  UpdateStatus UpdateShaderStages(VulkanShader* vertex_shader,
                                  VulkanShader* pixel_shader,
                                  PrimitiveType primitive_type);
  UpdateStatus UpdateVertexInputState(VulkanShader* vertex_shader);
  UpdateStatus UpdateInputAssemblyState(PrimitiveType primitive_type);
  UpdateStatus UpdateViewportState();
  UpdateStatus UpdateRasterizationState(PrimitiveType primitive_type);
  UpdateStatus UpdateMultisampleState();
  UpdateStatus UpdateDepthStencilState();
  UpdateStatus UpdateColorBlendState();

  bool SetShadowRegister(uint32_t* dest, uint32_t register_name);
  bool SetShadowRegister(float* dest, uint32_t register_name);
  bool SetShadowRegisterArray(uint32_t* dest, uint32_t num,
                              uint32_t register_name);

  struct UpdateRenderTargetsRegisters {
    uint32_t rb_modecontrol;
    reg::RB_SURFACE_INFO rb_surface_info;
    reg::RB_COLOR_INFO rb_color_info;
    reg::RB_DEPTH_INFO rb_depth_info;
    reg::RB_COLOR_INFO rb_color1_info;
    reg::RB_COLOR_INFO rb_color2_info;
    reg::RB_COLOR_INFO rb_color3_info;
    uint32_t rb_color_mask;
    uint32_t rb_depthcontrol;
    uint32_t rb_stencilrefmask;

    UpdateRenderTargetsRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_render_targets_regs_;

  struct UpdateShaderStagesRegisters {
    PrimitiveType primitive_type;
    uint32_t pa_su_sc_mode_cntl;
    uint32_t sq_program_cntl;
    VulkanShader* vertex_shader;
    VulkanShader* pixel_shader;

    UpdateShaderStagesRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_shader_stages_regs_;
  VkPipelineShaderStageCreateInfo update_shader_stages_info_[3];
  uint32_t update_shader_stages_stage_count_ = 0;

  struct UpdateVertexInputStateRegisters {
    VulkanShader* vertex_shader;

    UpdateVertexInputStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_vertex_input_state_regs_;
  VkPipelineVertexInputStateCreateInfo update_vertex_input_state_info_;
  VkVertexInputBindingDescription update_vertex_input_state_binding_descrs_[32];
  VkVertexInputAttributeDescription
      update_vertex_input_state_attrib_descrs_[96];

  struct UpdateInputAssemblyStateRegisters {
    PrimitiveType primitive_type;
    uint32_t pa_su_sc_mode_cntl;
    uint32_t multi_prim_ib_reset_index;

    UpdateInputAssemblyStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_input_assembly_state_regs_;
  VkPipelineInputAssemblyStateCreateInfo update_input_assembly_state_info_;

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
  VkPipelineViewportStateCreateInfo update_viewport_state_info_;

  struct UpdateRasterizationStateRegisters {
    PrimitiveType primitive_type;
    uint32_t pa_cl_clip_cntl;
    uint32_t pa_su_sc_mode_cntl;
    uint32_t pa_sc_screen_scissor_tl;
    uint32_t pa_sc_screen_scissor_br;
    uint32_t pa_sc_viz_query;
    uint32_t multi_prim_ib_reset_index;

    UpdateRasterizationStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_rasterization_state_regs_;
  VkPipelineRasterizationStateCreateInfo update_rasterization_state_info_;

  struct UpdateMultisampleStateeRegisters {
    uint32_t pa_sc_aa_config;
    uint32_t pa_su_sc_mode_cntl;
    uint32_t rb_surface_info;

    UpdateMultisampleStateeRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_multisample_state_regs_;
  VkPipelineMultisampleStateCreateInfo update_multisample_state_info_;

  struct UpdateDepthStencilStateRegisters {
    uint32_t rb_depthcontrol;
    uint32_t rb_stencilrefmask;

    UpdateDepthStencilStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_depth_stencil_state_regs_;
  VkPipelineDepthStencilStateCreateInfo update_depth_stencil_state_info_;

  struct UpdateColorBlendStateRegisters {
    uint32_t rb_colorcontrol;
    uint32_t rb_color_mask;
    uint32_t rb_blendcontrol[4];
    uint32_t rb_modecontrol;

    UpdateColorBlendStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_color_blend_state_regs_;
  VkPipelineColorBlendStateCreateInfo update_color_blend_state_info_;
  VkPipelineColorBlendAttachmentState update_color_blend_attachment_states_[4];

  struct SetDynamicStateRegisters {
    uint32_t pa_sc_window_offset;

    uint32_t pa_su_sc_mode_cntl;
    uint32_t pa_sc_window_scissor_tl;
    uint32_t pa_sc_window_scissor_br;

    uint32_t rb_surface_info;
    uint32_t pa_su_sc_vtx_cntl;
    uint32_t pa_cl_vte_cntl;
    float pa_cl_vport_xoffset;
    float pa_cl_vport_yoffset;
    float pa_cl_vport_zoffset;
    float pa_cl_vport_xscale;
    float pa_cl_vport_yscale;
    float pa_cl_vport_zscale;

    float rb_blend_rgba[4];
    uint32_t rb_stencilrefmask;

    uint32_t sq_program_cntl;
    uint32_t sq_context_misc;
    uint32_t rb_colorcontrol;
    float rb_alpha_ref;
    uint32_t pa_su_point_size;

    SetDynamicStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } set_dynamic_state_registers_;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_PIPELINE_CACHE_H_
