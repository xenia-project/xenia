/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/pipeline_cache.h"

#include "third_party/xxhash/xxhash.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"

#include <cinttypes>
#include <string>

namespace xe {
namespace gpu {
namespace vulkan {

using xe::ui::vulkan::CheckResult;

// Generated with `xenia-build genspirv`.
#include "xenia/gpu/vulkan/shaders/bin/dummy_frag.h"
#include "xenia/gpu/vulkan/shaders/bin/line_quad_list_geom.h"
#include "xenia/gpu/vulkan/shaders/bin/point_list_geom.h"
#include "xenia/gpu/vulkan/shaders/bin/quad_list_geom.h"
#include "xenia/gpu/vulkan/shaders/bin/rect_list_geom.h"

PipelineCache::PipelineCache(RegisterFile* register_file,
                             ui::vulkan::VulkanDevice* device)
    : register_file_(register_file), device_(device) {
  // We can also use the GLSL translator with a Vulkan dialect.
  shader_translator_.reset(new SpirvShaderTranslator());
}

PipelineCache::~PipelineCache() { Shutdown(); }

VkResult PipelineCache::Initialize(
    VkDescriptorSetLayout uniform_descriptor_set_layout,
    VkDescriptorSetLayout texture_descriptor_set_layout,
    VkDescriptorSetLayout vertex_descriptor_set_layout) {
  VkResult status;

  // Initialize the shared driver pipeline cache.
  // We'll likely want to serialize this and reuse it, if that proves to be
  // useful. If the shaders are expensive and this helps we could do it per
  // game, otherwise a single shared cache for render state/etc.
  VkPipelineCacheCreateInfo pipeline_cache_info;
  pipeline_cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  pipeline_cache_info.pNext = nullptr;
  pipeline_cache_info.flags = 0;
  pipeline_cache_info.initialDataSize = 0;
  pipeline_cache_info.pInitialData = nullptr;
  status = vkCreatePipelineCache(*device_, &pipeline_cache_info, nullptr,
                                 &pipeline_cache_);
  if (status != VK_SUCCESS) {
    return status;
  }

  // Descriptors used by the pipelines.
  // These are the only ones we can ever bind.
  VkDescriptorSetLayout set_layouts[] = {
      // Per-draw constant register uniforms.
      uniform_descriptor_set_layout,
      // All texture bindings.
      texture_descriptor_set_layout,
      // Vertex bindings.
      vertex_descriptor_set_layout,
  };

  // Push constants used for draw parameters.
  // We need to keep these under 128b across all stages.
  // TODO(benvanik): split between the stages?
  VkPushConstantRange push_constant_ranges[1];
  push_constant_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                       VK_SHADER_STAGE_GEOMETRY_BIT |
                                       VK_SHADER_STAGE_FRAGMENT_BIT;
  push_constant_ranges[0].offset = 0;
  push_constant_ranges[0].size = kSpirvPushConstantsSize;

  // Shared pipeline layout.
  VkPipelineLayoutCreateInfo pipeline_layout_info;
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.pNext = nullptr;
  pipeline_layout_info.flags = 0;
  pipeline_layout_info.setLayoutCount =
      static_cast<uint32_t>(xe::countof(set_layouts));
  pipeline_layout_info.pSetLayouts = set_layouts;
  pipeline_layout_info.pushConstantRangeCount =
      static_cast<uint32_t>(xe::countof(push_constant_ranges));
  pipeline_layout_info.pPushConstantRanges = push_constant_ranges;
  status = vkCreatePipelineLayout(*device_, &pipeline_layout_info, nullptr,
                                  &pipeline_layout_);
  if (status != VK_SUCCESS) {
    return status;
  }

  // Initialize our shared geometry shaders.
  // These will be used as needed to emulate primitive types Vulkan doesn't
  // support.
  VkShaderModuleCreateInfo shader_module_info;
  shader_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_module_info.pNext = nullptr;
  shader_module_info.flags = 0;
  shader_module_info.codeSize =
      static_cast<uint32_t>(sizeof(line_quad_list_geom));
  shader_module_info.pCode =
      reinterpret_cast<const uint32_t*>(line_quad_list_geom);
  status = vkCreateShaderModule(*device_, &shader_module_info, nullptr,
                                &geometry_shaders_.line_quad_list);
  if (status != VK_SUCCESS) {
    return status;
  }

  shader_module_info.codeSize = static_cast<uint32_t>(sizeof(point_list_geom));
  shader_module_info.pCode = reinterpret_cast<const uint32_t*>(point_list_geom);
  status = vkCreateShaderModule(*device_, &shader_module_info, nullptr,
                                &geometry_shaders_.point_list);
  if (status != VK_SUCCESS) {
    return status;
  }

  shader_module_info.codeSize = static_cast<uint32_t>(sizeof(quad_list_geom));
  shader_module_info.pCode = reinterpret_cast<const uint32_t*>(quad_list_geom);
  status = vkCreateShaderModule(*device_, &shader_module_info, nullptr,
                                &geometry_shaders_.quad_list);
  if (status != VK_SUCCESS) {
    return status;
  }

  shader_module_info.codeSize = static_cast<uint32_t>(sizeof(rect_list_geom));
  shader_module_info.pCode = reinterpret_cast<const uint32_t*>(rect_list_geom);
  status = vkCreateShaderModule(*device_, &shader_module_info, nullptr,
                                &geometry_shaders_.rect_list);
  if (status != VK_SUCCESS) {
    return status;
  }

  shader_module_info.codeSize = static_cast<uint32_t>(sizeof(dummy_frag));
  shader_module_info.pCode = reinterpret_cast<const uint32_t*>(dummy_frag);
  status = vkCreateShaderModule(*device_, &shader_module_info, nullptr,
                                &dummy_pixel_shader_);
  if (status != VK_SUCCESS) {
    return status;
  }

  return VK_SUCCESS;
}

void PipelineCache::Shutdown() {
  ClearCache();

  // Destroy geometry shaders.
  if (geometry_shaders_.line_quad_list) {
    vkDestroyShaderModule(*device_, geometry_shaders_.line_quad_list, nullptr);
    geometry_shaders_.line_quad_list = nullptr;
  }
  if (geometry_shaders_.point_list) {
    vkDestroyShaderModule(*device_, geometry_shaders_.point_list, nullptr);
    geometry_shaders_.point_list = nullptr;
  }
  if (geometry_shaders_.quad_list) {
    vkDestroyShaderModule(*device_, geometry_shaders_.quad_list, nullptr);
    geometry_shaders_.quad_list = nullptr;
  }
  if (geometry_shaders_.rect_list) {
    vkDestroyShaderModule(*device_, geometry_shaders_.rect_list, nullptr);
    geometry_shaders_.rect_list = nullptr;
  }
  if (dummy_pixel_shader_) {
    vkDestroyShaderModule(*device_, dummy_pixel_shader_, nullptr);
    dummy_pixel_shader_ = nullptr;
  }

  if (pipeline_layout_) {
    vkDestroyPipelineLayout(*device_, pipeline_layout_, nullptr);
    pipeline_layout_ = nullptr;
  }
  if (pipeline_cache_) {
    vkDestroyPipelineCache(*device_, pipeline_cache_, nullptr);
    pipeline_cache_ = nullptr;
  }
}

VulkanShader* PipelineCache::LoadShader(ShaderType shader_type,
                                        uint32_t guest_address,
                                        const uint32_t* host_address,
                                        uint32_t dword_count) {
  // Hash the input memory and lookup the shader.
  uint64_t data_hash = XXH64(host_address, dword_count * sizeof(uint32_t), 0);
  auto it = shader_map_.find(data_hash);
  if (it != shader_map_.end()) {
    // Shader has been previously loaded.
    return it->second;
  }

  // Always create the shader and stash it away.
  // We need to track it even if it fails translation so we know not to try
  // again.
  VulkanShader* shader = new VulkanShader(device_, shader_type, data_hash,
                                          host_address, dword_count);
  shader_map_.insert({data_hash, shader});

  return shader;
}

PipelineCache::UpdateStatus PipelineCache::ConfigurePipeline(
    VkCommandBuffer command_buffer, const RenderState* render_state,
    VulkanShader* vertex_shader, VulkanShader* pixel_shader,
    PrimitiveType primitive_type, VkPipeline* pipeline_out) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  assert_not_null(pipeline_out);

  // Perform a pass over all registers and state updating our cached structures.
  // This will tell us if anything has changed that requires us to either build
  // a new pipeline or use an existing one.
  VkPipeline pipeline = nullptr;
  auto update_status = UpdateState(vertex_shader, pixel_shader, primitive_type);
  switch (update_status) {
    case UpdateStatus::kCompatible:
      // Requested pipeline is compatible with our previous one, so use that.
      // Note that there still may be dynamic state that needs updating.
      pipeline = current_pipeline_;
      break;
    case UpdateStatus::kMismatch:
      // Pipeline state has changed. We need to either create a new one or find
      // an old one that matches.
      current_pipeline_ = nullptr;
      break;
    case UpdateStatus::kError:
      // Error updating state - bail out.
      // We are in an indeterminate state, so reset things for the next attempt.
      current_pipeline_ = nullptr;
      return update_status;
  }
  if (!pipeline) {
    // Should have a hash key produced by the UpdateState pass.
    uint64_t hash_key = XXH64_digest(&hash_state_);
    pipeline = GetPipeline(render_state, hash_key);
    current_pipeline_ = pipeline;
    if (!pipeline) {
      // Unable to create pipeline.
      return UpdateStatus::kError;
    }
  }

  *pipeline_out = pipeline;
  return update_status;
}

void PipelineCache::ClearCache() {
  // Destroy all pipelines.
  for (auto it : cached_pipelines_) {
    vkDestroyPipeline(*device_, it.second, nullptr);
  }
  cached_pipelines_.clear();
  COUNT_profile_set("gpu/pipeline_cache/pipelines", 0);

  // Destroy all shaders.
  for (auto it : shader_map_) {
    delete it.second;
  }
  shader_map_.clear();
}

VkPipeline PipelineCache::GetPipeline(const RenderState* render_state,
                                      uint64_t hash_key) {
  // Lookup the pipeline in the cache.
  auto it = cached_pipelines_.find(hash_key);
  if (it != cached_pipelines_.end()) {
    // Found existing pipeline.
    return it->second;
  }

  VkPipelineDynamicStateCreateInfo dynamic_state_info;
  dynamic_state_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state_info.pNext = nullptr;
  dynamic_state_info.flags = 0;
  VkDynamicState dynamic_states[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
      VK_DYNAMIC_STATE_LINE_WIDTH,
      VK_DYNAMIC_STATE_DEPTH_BIAS,
      VK_DYNAMIC_STATE_BLEND_CONSTANTS,
      VK_DYNAMIC_STATE_DEPTH_BOUNDS,
      VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
      VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
      VK_DYNAMIC_STATE_STENCIL_REFERENCE,
  };
  dynamic_state_info.dynamicStateCount =
      static_cast<uint32_t>(xe::countof(dynamic_states));
  dynamic_state_info.pDynamicStates = dynamic_states;

  VkGraphicsPipelineCreateInfo pipeline_info;
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.pNext = nullptr;
  pipeline_info.flags = VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;
  pipeline_info.stageCount = update_shader_stages_stage_count_;
  pipeline_info.pStages = update_shader_stages_info_;
  pipeline_info.pVertexInputState = &update_vertex_input_state_info_;
  pipeline_info.pInputAssemblyState = &update_input_assembly_state_info_;
  pipeline_info.pTessellationState = nullptr;
  pipeline_info.pViewportState = &update_viewport_state_info_;
  pipeline_info.pRasterizationState = &update_rasterization_state_info_;
  pipeline_info.pMultisampleState = &update_multisample_state_info_;
  pipeline_info.pDepthStencilState = &update_depth_stencil_state_info_;
  pipeline_info.pColorBlendState = &update_color_blend_state_info_;
  pipeline_info.pDynamicState = &dynamic_state_info;
  pipeline_info.layout = pipeline_layout_;
  pipeline_info.renderPass = render_state->render_pass_handle;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = nullptr;
  pipeline_info.basePipelineIndex = -1;
  VkPipeline pipeline = nullptr;
  auto result = vkCreateGraphicsPipelines(*device_, pipeline_cache_, 1,
                                          &pipeline_info, nullptr, &pipeline);
  if (result != VK_SUCCESS) {
    XELOGE("vkCreateGraphicsPipelines failed with code %d", result);
    assert_always();
    return nullptr;
  }

  // Dump shader disassembly.
  if (FLAGS_vulkan_dump_disasm) {
    if (device_->HasEnabledExtension(VK_AMD_SHADER_INFO_EXTENSION_NAME)) {
      DumpShaderDisasmAMD(pipeline);
    } else if (device_->device_info().properties.vendorID == 0x10DE) {
      // NVIDIA cards
      DumpShaderDisasmNV(pipeline_info);
    }
  }

  // Add to cache with the hash key for reuse.
  cached_pipelines_.insert({hash_key, pipeline});
  COUNT_profile_set("gpu/pipeline_cache/pipelines", cached_pipelines_.size());

  return pipeline;
}

bool PipelineCache::TranslateShader(VulkanShader* shader,
                                    xenos::xe_gpu_program_cntl_t cntl) {
  // Perform translation.
  // If this fails the shader will be marked as invalid and ignored later.
  if (!shader_translator_->Translate(shader, cntl)) {
    XELOGE("Shader translation failed; marking shader as ignored");
    return false;
  }

  // Prepare the shader for use (creates our VkShaderModule).
  // It could still fail at this point.
  if (!shader->Prepare()) {
    XELOGE("Shader preparation failed; marking shader as ignored");
    return false;
  }

  if (shader->is_valid()) {
    XELOGGPU("Generated %s shader (%db) - hash %.16" PRIX64 ":\n%s\n",
             shader->type() == ShaderType::kVertex ? "vertex" : "pixel",
             shader->ucode_dword_count() * 4, shader->ucode_data_hash(),
             shader->ucode_disassembly().c_str());
  }

  // Dump shader files if desired.
  if (!FLAGS_dump_shaders.empty()) {
    shader->Dump(FLAGS_dump_shaders, "vk");
  }

  return shader->is_valid();
}

static void DumpShaderStatisticsAMD(const VkShaderStatisticsInfoAMD& stats) {
  XELOGI(" - resource usage:");
  XELOGI("   numUsedVgprs: %d", stats.resourceUsage.numUsedVgprs);
  XELOGI("   numUsedSgprs: %d", stats.resourceUsage.numUsedSgprs);
  XELOGI("   ldsSizePerLocalWorkGroup: %d",
         stats.resourceUsage.ldsSizePerLocalWorkGroup);
  XELOGI("   ldsUsageSizeInBytes     : %d",
         stats.resourceUsage.ldsUsageSizeInBytes);
  XELOGI("   scratchMemUsageInBytes  : %d",
         stats.resourceUsage.scratchMemUsageInBytes);
  XELOGI("numPhysicalVgprs : %d", stats.numPhysicalVgprs);
  XELOGI("numPhysicalSgprs : %d", stats.numPhysicalSgprs);
  XELOGI("numAvailableVgprs: %d", stats.numAvailableVgprs);
  XELOGI("numAvailableSgprs: %d", stats.numAvailableSgprs);
}

void PipelineCache::DumpShaderDisasmAMD(VkPipeline pipeline) {
  auto fn_GetShaderInfoAMD = (PFN_vkGetShaderInfoAMD)vkGetDeviceProcAddr(
      *device_, "vkGetShaderInfoAMD");

  VkResult status = VK_SUCCESS;
  size_t data_size = 0;

  VkShaderStatisticsInfoAMD stats;
  data_size = sizeof(stats);

  // Vertex shader
  status = fn_GetShaderInfoAMD(*device_, pipeline, VK_SHADER_STAGE_VERTEX_BIT,
                               VK_SHADER_INFO_TYPE_STATISTICS_AMD, &data_size,
                               &stats);
  if (status == VK_SUCCESS) {
    XELOGI("AMD Vertex Shader Statistics:");
    DumpShaderStatisticsAMD(stats);
  }

  // Fragment shader
  status = fn_GetShaderInfoAMD(*device_, pipeline, VK_SHADER_STAGE_FRAGMENT_BIT,
                               VK_SHADER_INFO_TYPE_STATISTICS_AMD, &data_size,
                               &stats);
  if (status == VK_SUCCESS) {
    XELOGI("AMD Fragment Shader Statistics:");
    DumpShaderStatisticsAMD(stats);
  }

  // TODO(DrChat): Eventually dump the disasm...
}

void PipelineCache::DumpShaderDisasmNV(
    const VkGraphicsPipelineCreateInfo& pipeline_info) {
  // !! HACK !!: This only works on NVidia drivers. Dumps shader disasm.
  // This code is super ugly. Update this when NVidia includes an official
  // way to dump shader disassembly.

  VkPipelineCacheCreateInfo pipeline_cache_info;
  VkPipelineCache dummy_pipeline_cache;
  pipeline_cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  pipeline_cache_info.pNext = nullptr;
  pipeline_cache_info.flags = 0;
  pipeline_cache_info.initialDataSize = 0;
  pipeline_cache_info.pInitialData = nullptr;
  auto status = vkCreatePipelineCache(*device_, &pipeline_cache_info, nullptr,
                                      &dummy_pipeline_cache);
  CheckResult(status, "vkCreatePipelineCache");

  // Create a pipeline on the dummy cache and dump it.
  VkPipeline dummy_pipeline;
  status = vkCreateGraphicsPipelines(*device_, dummy_pipeline_cache, 1,
                                     &pipeline_info, nullptr, &dummy_pipeline);

  std::vector<uint8_t> pipeline_data;
  size_t data_size = 0;
  status = vkGetPipelineCacheData(*device_, dummy_pipeline_cache, &data_size,
                                  nullptr);
  if (status == VK_SUCCESS) {
    pipeline_data.resize(data_size);
    vkGetPipelineCacheData(*device_, dummy_pipeline_cache, &data_size,
                           pipeline_data.data());

    // Scan the data for the disassembly.
    std::string disasm_vp, disasm_fp;

    const char* disasm_start_vp = nullptr;
    const char* disasm_start_fp = nullptr;
    size_t search_offset = 0;
    const char* search_start =
        reinterpret_cast<const char*>(pipeline_data.data());
    while (true) {
      auto p = reinterpret_cast<const char*>(
          memchr(pipeline_data.data() + search_offset, '!',
                 pipeline_data.size() - search_offset));
      if (!p) {
        break;
      }
      if (!strncmp(p, "!!NV", 4)) {
        if (!strncmp(p + 4, "vp", 2)) {
          disasm_start_vp = p;
        } else if (!strncmp(p + 4, "fp", 2)) {
          disasm_start_fp = p;
        }

        if (disasm_start_fp && disasm_start_vp) {
          // Found all we needed.
          break;
        }
      }
      search_offset = p - search_start;
      ++search_offset;
    }
    if (disasm_start_vp) {
      disasm_vp = std::string(disasm_start_vp);

      // For some reason there's question marks all over the code.
      disasm_vp.erase(std::remove(disasm_vp.begin(), disasm_vp.end(), '?'),
                      disasm_vp.end());
    } else {
      disasm_vp = std::string("Shader disassembly not available.");
    }

    if (disasm_start_fp) {
      disasm_fp = std::string(disasm_start_fp);

      // For some reason there's question marks all over the code.
      disasm_fp.erase(std::remove(disasm_fp.begin(), disasm_fp.end(), '?'),
                      disasm_fp.end());
    } else {
      disasm_fp = std::string("Shader disassembly not available.");
    }

    XELOGI("%s\n=====================================\n%s\n", disasm_vp.c_str(),
           disasm_fp.c_str());
  }

  vkDestroyPipeline(*device_, dummy_pipeline, nullptr);
  vkDestroyPipelineCache(*device_, dummy_pipeline_cache, nullptr);
}

VkShaderModule PipelineCache::GetGeometryShader(PrimitiveType primitive_type,
                                                bool is_line_mode) {
  switch (primitive_type) {
    case PrimitiveType::kLineList:
    case PrimitiveType::kLineLoop:
    case PrimitiveType::kLineStrip:
    case PrimitiveType::kTriangleList:
    case PrimitiveType::kTriangleFan:
    case PrimitiveType::kTriangleStrip:
      // Supported directly - no need to emulate.
      return nullptr;
    case PrimitiveType::kPointList:
      return geometry_shaders_.point_list;
    case PrimitiveType::kTriangleWithWFlags:
      assert_always("Unknown geometry type");
      return nullptr;
    case PrimitiveType::kRectangleList:
      return geometry_shaders_.rect_list;
    case PrimitiveType::kQuadList:
      return is_line_mode ? geometry_shaders_.line_quad_list
                          : geometry_shaders_.quad_list;
    case PrimitiveType::kQuadStrip:
      // TODO(benvanik): quad strip geometry shader.
      assert_always("Quad strips not implemented");
      return nullptr;
    case PrimitiveType::k2DCopyRectListV0:
    case PrimitiveType::k2DCopyRectListV1:
    case PrimitiveType::k2DCopyRectListV2:
    case PrimitiveType::k2DCopyRectListV3:
      // TODO(DrChat): Research this.
      return nullptr;
    default:
      assert_unhandled_case(primitive_type);
      return nullptr;
  }
}

bool PipelineCache::SetDynamicState(VkCommandBuffer command_buffer,
                                    bool full_update) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  auto& regs = set_dynamic_state_registers_;

  bool window_offset_dirty = SetShadowRegister(&regs.pa_sc_window_offset,
                                               XE_GPU_REG_PA_SC_WINDOW_OFFSET);
  window_offset_dirty |= SetShadowRegister(&regs.pa_su_sc_mode_cntl,
                                           XE_GPU_REG_PA_SU_SC_MODE_CNTL);

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

  // VK_DYNAMIC_STATE_SCISSOR
  bool scissor_state_dirty = full_update || window_offset_dirty;
  scissor_state_dirty |= SetShadowRegister(&regs.pa_sc_window_scissor_tl,
                                           XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL);
  scissor_state_dirty |= SetShadowRegister(&regs.pa_sc_window_scissor_br,
                                           XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR);
  if (scissor_state_dirty) {
    int32_t ws_x = regs.pa_sc_window_scissor_tl & 0x7FFF;
    int32_t ws_y = (regs.pa_sc_window_scissor_tl >> 16) & 0x7FFF;
    int32_t ws_w = (regs.pa_sc_window_scissor_br & 0x7FFF) - ws_x;
    int32_t ws_h = ((regs.pa_sc_window_scissor_br >> 16) & 0x7FFF) - ws_y;
    ws_x += window_offset_x;
    ws_y += window_offset_y;

    int32_t adj_x = ws_x - std::max(ws_x, 0);
    int32_t adj_y = ws_y - std::max(ws_y, 0);

    VkRect2D scissor_rect;
    scissor_rect.offset.x = ws_x - adj_x;
    scissor_rect.offset.y = ws_y - adj_y;
    scissor_rect.extent.width = std::max(ws_w + adj_x, 0);
    scissor_rect.extent.height = std::max(ws_h + adj_y, 0);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor_rect);
  }

  // VK_DYNAMIC_STATE_VIEWPORT
  bool viewport_state_dirty = full_update || window_offset_dirty;
  viewport_state_dirty |=
      SetShadowRegister(&regs.rb_surface_info, XE_GPU_REG_RB_SURFACE_INFO);
  viewport_state_dirty |=
      SetShadowRegister(&regs.pa_cl_vte_cntl, XE_GPU_REG_PA_CL_VTE_CNTL);
  viewport_state_dirty |=
      SetShadowRegister(&regs.pa_su_sc_vtx_cntl, XE_GPU_REG_PA_SU_VTX_CNTL);
  viewport_state_dirty |= SetShadowRegister(&regs.pa_cl_vport_xoffset,
                                            XE_GPU_REG_PA_CL_VPORT_XOFFSET);
  viewport_state_dirty |= SetShadowRegister(&regs.pa_cl_vport_yoffset,
                                            XE_GPU_REG_PA_CL_VPORT_YOFFSET);
  viewport_state_dirty |= SetShadowRegister(&regs.pa_cl_vport_zoffset,
                                            XE_GPU_REG_PA_CL_VPORT_ZOFFSET);
  viewport_state_dirty |= SetShadowRegister(&regs.pa_cl_vport_xscale,
                                            XE_GPU_REG_PA_CL_VPORT_XSCALE);
  viewport_state_dirty |= SetShadowRegister(&regs.pa_cl_vport_yscale,
                                            XE_GPU_REG_PA_CL_VPORT_YSCALE);
  viewport_state_dirty |= SetShadowRegister(&regs.pa_cl_vport_zscale,
                                            XE_GPU_REG_PA_CL_VPORT_ZSCALE);
  // RB_SURFACE_INFO
  auto surface_msaa =
      static_cast<MsaaSamples>((regs.rb_surface_info >> 16) & 0x3);

  // Apply a multiplier to emulate MSAA.
  float window_width_scalar = 1;
  float window_height_scalar = 1;
  switch (surface_msaa) {
    case MsaaSamples::k1X:
      break;
    case MsaaSamples::k2X:
      window_height_scalar = 2;
      break;
    case MsaaSamples::k4X:
      window_width_scalar = window_height_scalar = 2;
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

  float vpw, vph, vpx, vpy;
  if (vport_xscale_enable) {
    float vox = vport_xoffset_enable ? regs.pa_cl_vport_xoffset : 0;
    float voy = vport_yoffset_enable ? regs.pa_cl_vport_yoffset : 0;
    float vsx = vport_xscale_enable ? regs.pa_cl_vport_xscale : 1;
    float vsy = vport_yscale_enable ? regs.pa_cl_vport_yscale : 1;

    window_width_scalar = window_height_scalar = 1;
    vpw = 2 * window_width_scalar * vsx;
    vph = -2 * window_height_scalar * vsy;
    vpx = window_width_scalar * vox - vpw / 2 + window_offset_x;
    vpy = window_height_scalar * voy - vph / 2 + window_offset_y;
  } else {
    vpw = 2 * 2560.0f * window_width_scalar;
    vph = 2 * 2560.0f * window_height_scalar;
    vpx = -2560.0f * window_width_scalar + window_offset_x;
    vpy = -2560.0f * window_height_scalar + window_offset_y;
  }

  if (viewport_state_dirty) {
    // float texel_offset_x = regs.pa_su_sc_vtx_cntl & 0x01 ? 0.5f : 0.f;
    // float texel_offset_y = regs.pa_su_sc_vtx_cntl & 0x01 ? 0.5f : 0.f;
    float texel_offset_x = 0.f;
    float texel_offset_y = 0.f;

    VkViewport viewport_rect;
    std::memset(&viewport_rect, 0, sizeof(VkViewport));
    viewport_rect.x = vpx + texel_offset_x;
    viewport_rect.y = vpy + texel_offset_y;
    viewport_rect.width = vpw;
    viewport_rect.height = vph;

    float voz = vport_zoffset_enable ? regs.pa_cl_vport_zoffset : 0;
    float vsz = vport_zscale_enable ? regs.pa_cl_vport_zscale : 1;
    viewport_rect.minDepth = voz;
    viewport_rect.maxDepth = voz + vsz;
    assert_true(viewport_rect.minDepth >= 0 && viewport_rect.minDepth <= 1);
    assert_true(viewport_rect.maxDepth >= -1 && viewport_rect.maxDepth <= 1);

    vkCmdSetViewport(command_buffer, 0, 1, &viewport_rect);
  }

  // VK_DYNAMIC_STATE_BLEND_CONSTANTS
  bool blend_constant_state_dirty = full_update;
  blend_constant_state_dirty |=
      SetShadowRegister(&regs.rb_blend_rgba[0], XE_GPU_REG_RB_BLEND_RED);
  blend_constant_state_dirty |=
      SetShadowRegister(&regs.rb_blend_rgba[1], XE_GPU_REG_RB_BLEND_GREEN);
  blend_constant_state_dirty |=
      SetShadowRegister(&regs.rb_blend_rgba[2], XE_GPU_REG_RB_BLEND_BLUE);
  blend_constant_state_dirty |=
      SetShadowRegister(&regs.rb_blend_rgba[3], XE_GPU_REG_RB_BLEND_ALPHA);
  if (blend_constant_state_dirty) {
    vkCmdSetBlendConstants(command_buffer, regs.rb_blend_rgba);
  }

  bool stencil_state_dirty = full_update;
  stencil_state_dirty |=
      SetShadowRegister(&regs.rb_stencilrefmask, XE_GPU_REG_RB_STENCILREFMASK);
  if (stencil_state_dirty) {
    uint32_t stencil_ref = (regs.rb_stencilrefmask & 0xFF);
    uint32_t stencil_read_mask = (regs.rb_stencilrefmask >> 8) & 0xFF;
    uint32_t stencil_write_mask = (regs.rb_stencilrefmask >> 16) & 0xFF;

    // VK_DYNAMIC_STATE_STENCIL_REFERENCE
    vkCmdSetStencilReference(command_buffer, VK_STENCIL_FRONT_AND_BACK,
                             stencil_ref);

    // VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK
    vkCmdSetStencilCompareMask(command_buffer, VK_STENCIL_FRONT_AND_BACK,
                               stencil_read_mask);

    // VK_DYNAMIC_STATE_STENCIL_WRITE_MASK
    vkCmdSetStencilWriteMask(command_buffer, VK_STENCIL_FRONT_AND_BACK,
                             stencil_write_mask);
  }

  bool push_constants_dirty = full_update || viewport_state_dirty;
  push_constants_dirty |=
      SetShadowRegister(&regs.sq_program_cntl, XE_GPU_REG_SQ_PROGRAM_CNTL);
  push_constants_dirty |=
      SetShadowRegister(&regs.sq_context_misc, XE_GPU_REG_SQ_CONTEXT_MISC);
  push_constants_dirty |=
      SetShadowRegister(&regs.rb_colorcontrol, XE_GPU_REG_RB_COLORCONTROL);
  push_constants_dirty |=
      SetShadowRegister(&regs.rb_alpha_ref, XE_GPU_REG_RB_ALPHA_REF);
  push_constants_dirty |=
      SetShadowRegister(&regs.pa_su_point_size, XE_GPU_REG_PA_SU_POINT_SIZE);
  if (push_constants_dirty) {
    xenos::xe_gpu_program_cntl_t program_cntl;
    program_cntl.dword_0 = regs.sq_program_cntl;

    // Normal vertex shaders only, for now.
    // TODO(benvanik): transform feedback/memexport.
    // https://github.com/freedreno/freedreno/blob/master/includes/a2xx.xml.h
    // Draw calls skipped if they have unsupported export modes.
    // 0 = positionOnly
    // 1 = unused
    // 2 = sprite
    // 3 = edge
    // 4 = kill
    // 5 = spriteKill
    // 6 = edgeKill
    // 7 = multipass
    assert_true(program_cntl.vs_export_mode == 0 ||
                program_cntl.vs_export_mode == 2 ||
                program_cntl.vs_export_mode == 7);
    assert_false(program_cntl.gen_index_vtx);

    SpirvPushConstants push_constants;

    // Done in VS, no need to flush state.
    if ((regs.pa_cl_vte_cntl & (1 << 0)) > 0) {
      push_constants.window_scale[0] = 1.0f;
      push_constants.window_scale[1] = -1.0f;
    } else {
      push_constants.window_scale[0] = 1.0f / 2560.0f;
      push_constants.window_scale[1] = 1.0f / 2560.0f;
    }
    push_constants.window_scale[2] = vpw;
    push_constants.window_scale[3] = vph;

    // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf
    // VTX_XY_FMT = true: the incoming XY have already been multiplied by 1/W0.
    //            = false: multiply the X, Y coordinates by 1/W0.
    // VTX_Z_FMT = true: the incoming Z has already been multiplied by 1/W0.
    //           = false: multiply the Z coordinate by 1/W0.
    // VTX_W0_FMT = true: the incoming W0 is not 1/W0. Perform the reciprocal to
    //                    get 1/W0.
    float vtx_xy_fmt = (regs.pa_cl_vte_cntl >> 8) & 0x1 ? 1.0f : 0.0f;
    float vtx_z_fmt = (regs.pa_cl_vte_cntl >> 9) & 0x1 ? 1.0f : 0.0f;
    float vtx_w0_fmt = (regs.pa_cl_vte_cntl >> 10) & 0x1 ? 1.0f : 0.0f;
    push_constants.vtx_fmt[0] = vtx_xy_fmt;
    push_constants.vtx_fmt[1] = vtx_xy_fmt;
    push_constants.vtx_fmt[2] = vtx_z_fmt;
    push_constants.vtx_fmt[3] = vtx_w0_fmt;

    // Point size
    push_constants.point_size[0] =
        static_cast<float>((regs.pa_su_point_size & 0xffff0000) >> 16) / 8.0f;
    push_constants.point_size[1] =
        static_cast<float>((regs.pa_su_point_size & 0x0000ffff)) / 8.0f;

    // Alpha testing -- ALPHAREF, ALPHAFUNC, ALPHATESTENABLE
    // Emulated in shader.
    // if(ALPHATESTENABLE && frag_out.a [<=/ALPHAFUNC] ALPHAREF) discard;
    // ALPHATESTENABLE
    push_constants.alpha_test[0] =
        (regs.rb_colorcontrol & 0x8) != 0 ? 1.0f : 0.0f;
    // ALPHAFUNC
    push_constants.alpha_test[1] =
        static_cast<float>(regs.rb_colorcontrol & 0x7);
    // ALPHAREF
    push_constants.alpha_test[2] = regs.rb_alpha_ref;

    // Whether to populate a register in the pixel shader with frag coord.
    int ps_param_gen = (regs.sq_context_misc >> 8) & 0xFF;
    push_constants.ps_param_gen = program_cntl.param_gen ? ps_param_gen : -1;

    vkCmdPushConstants(command_buffer, pipeline_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT |
                           VK_SHADER_STAGE_GEOMETRY_BIT |
                           VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, kSpirvPushConstantsSize, &push_constants);
  }

  if (full_update) {
    // VK_DYNAMIC_STATE_LINE_WIDTH
    vkCmdSetLineWidth(command_buffer, 1.0f);

    // VK_DYNAMIC_STATE_DEPTH_BIAS
    vkCmdSetDepthBias(command_buffer, 0.0f, 0.0f, 0.0f);

    // VK_DYNAMIC_STATE_DEPTH_BOUNDS
    vkCmdSetDepthBounds(command_buffer, 0.0f, 1.0f);
  }

  return true;
}

bool PipelineCache::SetShadowRegister(uint32_t* dest, uint32_t register_name) {
  uint32_t value = register_file_->values[register_name].u32;
  if (*dest == value) {
    return false;
  }
  *dest = value;
  return true;
}

bool PipelineCache::SetShadowRegister(float* dest, uint32_t register_name) {
  float value = register_file_->values[register_name].f32;
  if (*dest == value) {
    return false;
  }
  *dest = value;
  return true;
}

bool PipelineCache::SetShadowRegisterArray(uint32_t* dest, uint32_t num,
                                           uint32_t register_name) {
  bool dirty = false;
  for (uint32_t i = 0; i < num; i++) {
    uint32_t value = register_file_->values[register_name + i].u32;
    if (dest[i] == value) {
      continue;
    }

    dest[i] = value;
    dirty |= true;
  }

  return dirty;
}

PipelineCache::UpdateStatus PipelineCache::UpdateState(
    VulkanShader* vertex_shader, VulkanShader* pixel_shader,
    PrimitiveType primitive_type) {
  bool mismatch = false;

  // Reset hash so we can build it up.
  XXH64_reset(&hash_state_, 0);

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
  status = UpdateRenderTargetState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update render target state");
  status = UpdateShaderStages(vertex_shader, pixel_shader, primitive_type);
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update shader stages");
  status = UpdateVertexInputState(vertex_shader);
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update vertex input state");
  status = UpdateInputAssemblyState(primitive_type);
  CHECK_UPDATE_STATUS(status, mismatch,
                      "Unable to update input assembly state");
  status = UpdateViewportState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update viewport state");
  status = UpdateRasterizationState(primitive_type);
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update rasterization state");
  status = UpdateMultisampleState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update multisample state");
  status = UpdateDepthStencilState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update depth/stencil state");
  status = UpdateColorBlendState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update color blend state");

  return mismatch ? UpdateStatus::kMismatch : UpdateStatus::kCompatible;
}

PipelineCache::UpdateStatus PipelineCache::UpdateRenderTargetState() {
  auto& regs = update_render_targets_regs_;
  bool dirty = false;

  // Check the render target formats
  struct {
    reg::RB_COLOR_INFO rb_color_info;
    reg::RB_DEPTH_INFO rb_depth_info;
    reg::RB_COLOR_INFO rb_color1_info;
    reg::RB_COLOR_INFO rb_color2_info;
    reg::RB_COLOR_INFO rb_color3_info;
  }* cur_regs = reinterpret_cast<decltype(cur_regs)>(
      &register_file_->values[XE_GPU_REG_RB_COLOR_INFO].u32);

  dirty |=
      regs.rb_color_info.color_format != cur_regs->rb_color_info.color_format;
  dirty |=
      regs.rb_depth_info.depth_format != cur_regs->rb_depth_info.depth_format;
  dirty |=
      regs.rb_color1_info.color_format != cur_regs->rb_color1_info.color_format;
  dirty |=
      regs.rb_color2_info.color_format != cur_regs->rb_color2_info.color_format;
  dirty |=
      regs.rb_color3_info.color_format != cur_regs->rb_color3_info.color_format;

  // And copy the regs over.
  regs.rb_color_info.color_format = cur_regs->rb_color_info.color_format;
  regs.rb_depth_info.depth_format = cur_regs->rb_depth_info.depth_format;
  regs.rb_color1_info.color_format = cur_regs->rb_color1_info.color_format;
  regs.rb_color2_info.color_format = cur_regs->rb_color2_info.color_format;
  regs.rb_color3_info.color_format = cur_regs->rb_color3_info.color_format;
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateShaderStages(
    VulkanShader* vertex_shader, VulkanShader* pixel_shader,
    PrimitiveType primitive_type) {
  auto& regs = update_shader_stages_regs_;

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
  dirty |= regs.vertex_shader != vertex_shader;
  dirty |= regs.pixel_shader != pixel_shader;
  dirty |= regs.primitive_type != primitive_type;
  regs.vertex_shader = vertex_shader;
  regs.pixel_shader = pixel_shader;
  regs.primitive_type = primitive_type;
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  xenos::xe_gpu_program_cntl_t sq_program_cntl;
  sq_program_cntl.dword_0 = regs.sq_program_cntl;

  if (!vertex_shader->is_translated() &&
      !TranslateShader(vertex_shader, sq_program_cntl)) {
    XELOGE("Failed to translate the vertex shader!");
    return UpdateStatus::kError;
  }

  if (pixel_shader && !pixel_shader->is_translated() &&
      !TranslateShader(pixel_shader, sq_program_cntl)) {
    XELOGE("Failed to translate the pixel shader!");
    return UpdateStatus::kError;
  }

  update_shader_stages_stage_count_ = 0;

  auto& vertex_pipeline_stage =
      update_shader_stages_info_[update_shader_stages_stage_count_++];
  vertex_pipeline_stage.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertex_pipeline_stage.pNext = nullptr;
  vertex_pipeline_stage.flags = 0;
  vertex_pipeline_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertex_pipeline_stage.module = vertex_shader->shader_module();
  vertex_pipeline_stage.pName = "main";
  vertex_pipeline_stage.pSpecializationInfo = nullptr;

  bool is_line_mode = false;
  if (((regs.pa_su_sc_mode_cntl >> 3) & 0x3) != 0) {
    uint32_t front_poly_mode = (regs.pa_su_sc_mode_cntl >> 5) & 0x7;
    if (front_poly_mode == 1) {
      is_line_mode = true;
    }
  }
  auto geometry_shader = GetGeometryShader(primitive_type, is_line_mode);
  if (geometry_shader) {
    auto& geometry_pipeline_stage =
        update_shader_stages_info_[update_shader_stages_stage_count_++];
    geometry_pipeline_stage.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    geometry_pipeline_stage.pNext = nullptr;
    geometry_pipeline_stage.flags = 0;
    geometry_pipeline_stage.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    geometry_pipeline_stage.module = geometry_shader;
    geometry_pipeline_stage.pName = "main";
    geometry_pipeline_stage.pSpecializationInfo = nullptr;
  }

  auto& pixel_pipeline_stage =
      update_shader_stages_info_[update_shader_stages_stage_count_++];
  pixel_pipeline_stage.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pixel_pipeline_stage.pNext = nullptr;
  pixel_pipeline_stage.flags = 0;
  pixel_pipeline_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  pixel_pipeline_stage.module =
      pixel_shader ? pixel_shader->shader_module() : dummy_pixel_shader_;
  pixel_pipeline_stage.pName = "main";
  pixel_pipeline_stage.pSpecializationInfo = nullptr;

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateVertexInputState(
    VulkanShader* vertex_shader) {
  auto& regs = update_vertex_input_state_regs_;
  auto& state_info = update_vertex_input_state_info_;

  bool dirty = false;
  dirty |= vertex_shader != regs.vertex_shader;
  regs.vertex_shader = vertex_shader;
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  // We don't use vertex inputs.
  state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  state_info.pNext = nullptr;
  state_info.flags = 0;
  state_info.vertexBindingDescriptionCount = 0;
  state_info.vertexAttributeDescriptionCount = 0;
  state_info.pVertexBindingDescriptions = nullptr;
  state_info.pVertexAttributeDescriptions = nullptr;

  return UpdateStatus::kCompatible;
}

PipelineCache::UpdateStatus PipelineCache::UpdateInputAssemblyState(
    PrimitiveType primitive_type) {
  auto& regs = update_input_assembly_state_regs_;
  auto& state_info = update_input_assembly_state_info_;

  bool dirty = false;
  dirty |= primitive_type != regs.primitive_type;
  dirty |= SetShadowRegister(&regs.pa_su_sc_mode_cntl,
                             XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(&regs.multi_prim_ib_reset_index,
                             XE_GPU_REG_VGT_MULTI_PRIM_IB_RESET_INDX);
  regs.primitive_type = primitive_type;
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  state_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  state_info.pNext = nullptr;
  state_info.flags = 0;

  switch (primitive_type) {
    case PrimitiveType::kPointList:
      state_info.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
      break;
    case PrimitiveType::kLineList:
      state_info.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
      break;
    case PrimitiveType::kLineStrip:
      state_info.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
      break;
    case PrimitiveType::kLineLoop:
      state_info.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
      break;
    case PrimitiveType::kTriangleList:
      state_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      break;
    case PrimitiveType::kTriangleStrip:
      state_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
      break;
    case PrimitiveType::kTriangleFan:
      state_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
      break;
    case PrimitiveType::kRectangleList:
      state_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      break;
    case PrimitiveType::kQuadList:
      state_info.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
      break;
    default:
    case PrimitiveType::kTriangleWithWFlags:
      XELOGE("unsupported primitive type %d", primitive_type);
      assert_unhandled_case(primitive_type);
      return UpdateStatus::kError;
  }

  // TODO(benvanik): anything we can do about this? Vulkan seems to only support
  // first.
  assert_zero(regs.pa_su_sc_mode_cntl & (1 << 19));
  // if (regs.pa_su_sc_mode_cntl & (1 << 19)) {
  //   glProvokingVertex(GL_LAST_VERTEX_CONVENTION);
  // } else {
  //   glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
  // }

  // Primitive restart index is handled in the buffer cache.
  if (regs.pa_su_sc_mode_cntl & (1 << 21)) {
    state_info.primitiveRestartEnable = VK_TRUE;
  } else {
    state_info.primitiveRestartEnable = VK_FALSE;
  }

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateViewportState() {
  auto& state_info = update_viewport_state_info_;

  state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  state_info.pNext = nullptr;
  state_info.flags = 0;

  state_info.viewportCount = 1;
  state_info.scissorCount = 1;

  // Ignored; set dynamically.
  state_info.pViewports = nullptr;
  state_info.pScissors = nullptr;

  return UpdateStatus::kCompatible;
}

PipelineCache::UpdateStatus PipelineCache::UpdateRasterizationState(
    PrimitiveType primitive_type) {
  auto& regs = update_rasterization_state_regs_;
  auto& state_info = update_rasterization_state_info_;

  bool dirty = false;
  dirty |= regs.primitive_type != primitive_type;
  dirty |= SetShadowRegister(&regs.pa_cl_clip_cntl, XE_GPU_REG_PA_CL_CLIP_CNTL);
  dirty |= SetShadowRegister(&regs.pa_su_sc_mode_cntl,
                             XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(&regs.pa_sc_screen_scissor_tl,
                             XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL);
  dirty |= SetShadowRegister(&regs.pa_sc_screen_scissor_br,
                             XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR);
  dirty |= SetShadowRegister(&regs.pa_sc_viz_query, XE_GPU_REG_PA_SC_VIZ_QUERY);
  dirty |= SetShadowRegister(&regs.multi_prim_ib_reset_index,
                             XE_GPU_REG_VGT_MULTI_PRIM_IB_RESET_INDX);
  regs.primitive_type = primitive_type;
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  state_info.pNext = nullptr;
  state_info.flags = 0;

  // ZCLIP_NEAR_DISABLE
  // state_info.depthClampEnable = !(regs.pa_cl_clip_cntl & (1 << 26));
  // RASTERIZER_DISABLE
  // state_info.rasterizerDiscardEnable = !!(regs.pa_cl_clip_cntl & (1 << 22));

  // CLIP_DISABLE
  state_info.depthClampEnable = !!(regs.pa_cl_clip_cntl & (1 << 16));
  state_info.rasterizerDiscardEnable = VK_FALSE;

  bool poly_mode = ((regs.pa_su_sc_mode_cntl >> 3) & 0x3) != 0;
  if (poly_mode) {
    uint32_t front_poly_mode = (regs.pa_su_sc_mode_cntl >> 5) & 0x7;
    uint32_t back_poly_mode = (regs.pa_su_sc_mode_cntl >> 8) & 0x7;
    // Vulkan only supports both matching.
    assert_true(front_poly_mode == back_poly_mode);
    static const VkPolygonMode kFillModes[3] = {
        VK_POLYGON_MODE_POINT,
        VK_POLYGON_MODE_LINE,
        VK_POLYGON_MODE_FILL,
    };
    state_info.polygonMode = kFillModes[front_poly_mode];
  } else {
    state_info.polygonMode = VK_POLYGON_MODE_FILL;
  }

  switch (regs.pa_su_sc_mode_cntl & 0x3) {
    case 0:
      state_info.cullMode = VK_CULL_MODE_NONE;
      break;
    case 1:
      state_info.cullMode = VK_CULL_MODE_FRONT_BIT;
      break;
    case 2:
      state_info.cullMode = VK_CULL_MODE_BACK_BIT;
      break;
    case 3:
      // Cull both sides?
      assert_always();
      break;
  }
  if (regs.pa_su_sc_mode_cntl & 0x4) {
    state_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
  } else {
    state_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  }
  if (primitive_type == PrimitiveType::kRectangleList) {
    // Rectangle lists aren't culled. There may be other things they skip too.
    state_info.cullMode = VK_CULL_MODE_NONE;
  } else if (primitive_type == PrimitiveType::kPointList) {
    // Face culling doesn't apply to point primitives.
    state_info.cullMode = VK_CULL_MODE_NONE;
  }

  state_info.depthBiasEnable = VK_FALSE;

  // Ignored; set dynamically:
  state_info.depthBiasConstantFactor = 0;
  state_info.depthBiasClamp = 0;
  state_info.depthBiasSlopeFactor = 0;
  state_info.lineWidth = 1.0f;

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateMultisampleState() {
  auto& regs = update_multisample_state_regs_;
  auto& state_info = update_multisample_state_info_;

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.pa_sc_aa_config, XE_GPU_REG_PA_SC_AA_CONFIG);
  dirty |= SetShadowRegister(&regs.pa_su_sc_mode_cntl,
                             XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(&regs.rb_surface_info, XE_GPU_REG_RB_SURFACE_INFO);
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  state_info.pNext = nullptr;
  state_info.flags = 0;

  // PA_SC_AA_CONFIG MSAA_NUM_SAMPLES (0x7)
  // PA_SC_AA_MASK (0xFFFF)
  // PA_SU_SC_MODE_CNTL MSAA_ENABLE (0x10000)
  // If set, all samples will be sampled at set locations. Otherwise, they're
  // all sampled from the pixel center.
  if (FLAGS_vulkan_native_msaa) {
    auto msaa_num_samples =
        static_cast<MsaaSamples>((regs.rb_surface_info >> 16) & 0x3);
    switch (msaa_num_samples) {
      case MsaaSamples::k1X:
        state_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        break;
      case MsaaSamples::k2X:
        state_info.rasterizationSamples = VK_SAMPLE_COUNT_2_BIT;
        break;
      case MsaaSamples::k4X:
        state_info.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
        break;
      default:
        assert_unhandled_case(msaa_num_samples);
        break;
    }
  } else {
    state_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  }

  state_info.sampleShadingEnable = VK_FALSE;
  state_info.minSampleShading = 0;
  state_info.pSampleMask = nullptr;
  state_info.alphaToCoverageEnable = VK_FALSE;
  state_info.alphaToOneEnable = VK_FALSE;

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateDepthStencilState() {
  auto& regs = update_depth_stencil_state_regs_;
  auto& state_info = update_depth_stencil_state_info_;

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.rb_depthcontrol, XE_GPU_REG_RB_DEPTHCONTROL);
  dirty |=
      SetShadowRegister(&regs.rb_stencilrefmask, XE_GPU_REG_RB_STENCILREFMASK);
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  state_info.pNext = nullptr;
  state_info.flags = 0;

  static const VkCompareOp compare_func_map[] = {
      /*  0 */ VK_COMPARE_OP_NEVER,
      /*  1 */ VK_COMPARE_OP_LESS,
      /*  2 */ VK_COMPARE_OP_EQUAL,
      /*  3 */ VK_COMPARE_OP_LESS_OR_EQUAL,
      /*  4 */ VK_COMPARE_OP_GREATER,
      /*  5 */ VK_COMPARE_OP_NOT_EQUAL,
      /*  6 */ VK_COMPARE_OP_GREATER_OR_EQUAL,
      /*  7 */ VK_COMPARE_OP_ALWAYS,
  };
  static const VkStencilOp stencil_op_map[] = {
      /*  0 */ VK_STENCIL_OP_KEEP,
      /*  1 */ VK_STENCIL_OP_ZERO,
      /*  2 */ VK_STENCIL_OP_REPLACE,
      /*  3 */ VK_STENCIL_OP_INCREMENT_AND_CLAMP,
      /*  4 */ VK_STENCIL_OP_DECREMENT_AND_CLAMP,
      /*  5 */ VK_STENCIL_OP_INVERT,
      /*  6 */ VK_STENCIL_OP_INCREMENT_AND_WRAP,
      /*  7 */ VK_STENCIL_OP_DECREMENT_AND_WRAP,
  };

  // Depth state
  // TODO: EARLY_Z_ENABLE (needs to be enabled in shaders)
  state_info.depthWriteEnable = !!(regs.rb_depthcontrol & 0x4);
  state_info.depthTestEnable = !!(regs.rb_depthcontrol & 0x2);
  state_info.stencilTestEnable = !!(regs.rb_depthcontrol & 0x1);

  state_info.depthCompareOp =
      compare_func_map[(regs.rb_depthcontrol >> 4) & 0x7];
  state_info.depthBoundsTestEnable = VK_FALSE;

  // Stencil state
  state_info.front.compareOp =
      compare_func_map[(regs.rb_depthcontrol >> 8) & 0x7];
  state_info.front.failOp = stencil_op_map[(regs.rb_depthcontrol >> 11) & 0x7];
  state_info.front.passOp = stencil_op_map[(regs.rb_depthcontrol >> 14) & 0x7];
  state_info.front.depthFailOp =
      stencil_op_map[(regs.rb_depthcontrol >> 17) & 0x7];

  // BACKFACE_ENABLE
  if (!!(regs.rb_depthcontrol & 0x80)) {
    state_info.back.compareOp =
        compare_func_map[(regs.rb_depthcontrol >> 20) & 0x7];
    state_info.back.failOp = stencil_op_map[(regs.rb_depthcontrol >> 23) & 0x7];
    state_info.back.passOp = stencil_op_map[(regs.rb_depthcontrol >> 26) & 0x7];
    state_info.back.depthFailOp =
        stencil_op_map[(regs.rb_depthcontrol >> 29) & 0x7];
  } else {
    // Back state is identical to front state.
    std::memcpy(&state_info.back, &state_info.front, sizeof(VkStencilOpState));
  }

  // Ignored; set dynamically.
  state_info.minDepthBounds = 0;
  state_info.maxDepthBounds = 0;
  state_info.front.compareMask = 0;
  state_info.front.writeMask = 0;
  state_info.front.reference = 0;
  state_info.back.compareMask = 0;
  state_info.back.writeMask = 0;
  state_info.back.reference = 0;

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateColorBlendState() {
  auto& regs = update_color_blend_state_regs_;
  auto& state_info = update_color_blend_state_info_;

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.rb_colorcontrol, XE_GPU_REG_RB_COLORCONTROL);
  dirty |= SetShadowRegister(&regs.rb_color_mask, XE_GPU_REG_RB_COLOR_MASK);
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[0], XE_GPU_REG_RB_BLENDCONTROL_0);
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[1], XE_GPU_REG_RB_BLENDCONTROL_1);
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[2], XE_GPU_REG_RB_BLENDCONTROL_2);
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[3], XE_GPU_REG_RB_BLENDCONTROL_3);
  dirty |= SetShadowRegister(&regs.rb_modecontrol, XE_GPU_REG_RB_MODECONTROL);
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  state_info.pNext = nullptr;
  state_info.flags = 0;

  state_info.logicOpEnable = VK_FALSE;
  state_info.logicOp = VK_LOGIC_OP_NO_OP;

  auto enable_mode = static_cast<xenos::ModeControl>(regs.rb_modecontrol & 0x7);

  static const VkBlendFactor kBlendFactorMap[] = {
      /*  0 */ VK_BLEND_FACTOR_ZERO,
      /*  1 */ VK_BLEND_FACTOR_ONE,
      /*  2 */ VK_BLEND_FACTOR_ZERO,  // ?
      /*  3 */ VK_BLEND_FACTOR_ZERO,  // ?
      /*  4 */ VK_BLEND_FACTOR_SRC_COLOR,
      /*  5 */ VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
      /*  6 */ VK_BLEND_FACTOR_SRC_ALPHA,
      /*  7 */ VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
      /*  8 */ VK_BLEND_FACTOR_DST_COLOR,
      /*  9 */ VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
      /* 10 */ VK_BLEND_FACTOR_DST_ALPHA,
      /* 11 */ VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
      /* 12 */ VK_BLEND_FACTOR_CONSTANT_COLOR,
      /* 13 */ VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
      /* 14 */ VK_BLEND_FACTOR_CONSTANT_ALPHA,
      /* 15 */ VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
      /* 16 */ VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
  };
  static const VkBlendOp kBlendOpMap[] = {
      /*  0 */ VK_BLEND_OP_ADD,
      /*  1 */ VK_BLEND_OP_SUBTRACT,
      /*  2 */ VK_BLEND_OP_MIN,
      /*  3 */ VK_BLEND_OP_MAX,
      /*  4 */ VK_BLEND_OP_REVERSE_SUBTRACT,
  };
  auto& attachment_states = update_color_blend_attachment_states_;
  for (int i = 0; i < 4; ++i) {
    uint32_t blend_control = regs.rb_blendcontrol[i];
    auto& attachment_state = attachment_states[i];
    attachment_state.blendEnable = !(regs.rb_colorcontrol & 0x20);
    // A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND
    attachment_state.srcColorBlendFactor =
        kBlendFactorMap[(blend_control & 0x0000001F) >> 0];
    // A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND
    attachment_state.dstColorBlendFactor =
        kBlendFactorMap[(blend_control & 0x00001F00) >> 8];
    // A2XX_RB_BLEND_CONTROL_COLOR_COMB_FCN
    attachment_state.colorBlendOp =
        kBlendOpMap[(blend_control & 0x000000E0) >> 5];
    // A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND
    attachment_state.srcAlphaBlendFactor =
        kBlendFactorMap[(blend_control & 0x001F0000) >> 16];
    // A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND
    attachment_state.dstAlphaBlendFactor =
        kBlendFactorMap[(blend_control & 0x1F000000) >> 24];
    // A2XX_RB_BLEND_CONTROL_ALPHA_COMB_FCN
    attachment_state.alphaBlendOp =
        kBlendOpMap[(blend_control & 0x00E00000) >> 21];
    // A2XX_RB_COLOR_MASK_WRITE_* == D3DRS_COLORWRITEENABLE
    // Lines up with VkColorComponentFlagBits, where R=bit 1, G=bit 2, etc..
    uint32_t write_mask = (regs.rb_color_mask >> (i * 4)) & 0xF;
    attachment_state.colorWriteMask =
        enable_mode == xenos::ModeControl::kColorDepth ? write_mask : 0;
  }

  state_info.attachmentCount = 4;
  state_info.pAttachments = attachment_states;

  // Ignored; set dynamically.
  state_info.blendConstants[0] = 0.0f;
  state_info.blendConstants[1] = 0.0f;
  state_info.blendConstants[2] = 0.0f;
  state_info.blendConstants[3] = 0.0f;

  return UpdateStatus::kMismatch;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
