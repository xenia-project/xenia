/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_PIPELINE_STATE_CACHE_H_
#define XENIA_GPU_VULKAN_VULKAN_PIPELINE_STATE_CACHE_H_

#include <cstddef>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <utility>

#include "xenia/base/hash.h"
#include "xenia/base/platform.h"
#include "xenia/base/xxhash.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/vulkan/vulkan_render_target_cache.h"
#include "xenia/gpu/vulkan/vulkan_shader.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanCommandProcessor;

// TODO(Triang3l): Create a common base for both the Vulkan and the Direct3D
// implementations.
class VulkanPipelineCache {
 public:
  class PipelineLayoutProvider {
   public:
    virtual ~PipelineLayoutProvider() {}
    virtual VkPipelineLayout GetPipelineLayout() const = 0;
  };

  VulkanPipelineCache(VulkanCommandProcessor& command_processor,
                      const RegisterFile& register_file,
                      VulkanRenderTargetCache& render_target_cache);
  ~VulkanPipelineCache();

  bool Initialize();
  void Shutdown();
  void ClearCache();

  VulkanShader* LoadShader(xenos::ShaderType shader_type,
                           const uint32_t* host_address, uint32_t dword_count);
  // Analyze shader microcode on the translator thread.
  void AnalyzeShaderUcode(Shader& shader) {
    shader.AnalyzeUcode(ucode_disasm_buffer_);
  }

  // Retrieves the shader modification for the current state. The shader must
  // have microcode analyzed.
  SpirvShaderTranslator::Modification
  VulkanPipelineCache::GetCurrentVertexShaderModification(
      const Shader& shader,
      Shader::HostVertexShaderType host_vertex_shader_type) const;
  SpirvShaderTranslator::Modification
  VulkanPipelineCache::GetCurrentPixelShaderModification(
      const Shader& shader) const;

  // TODO(Triang3l): Return a deferred creation handle.
  bool ConfigurePipeline(VulkanShader::VulkanTranslation* vertex_shader,
                         VulkanShader::VulkanTranslation* pixel_shader,
                         VulkanRenderTargetCache::RenderPassKey render_pass_key,
                         VkPipeline& pipeline_out,
                         const PipelineLayoutProvider*& pipeline_layout_out);

 private:
  // Can only load pipeline storage if features of the device it was created on
  // and the current device match because descriptions may requires features not
  // supported on the device. Very radical differences (such as RB emulation
  // method) should result in a different storage file being used.
  union DevicePipelineFeatures {
    struct {
      uint32_t point_polygons : 1;
      uint32_t triangle_fans : 1;
    };
    uint32_t features = 0;
  };

  enum class PipelinePrimitiveTopology : uint32_t {
    kPointList,
    kLineList,
    kLineStrip,
    kTriangleList,
    kTriangleStrip,
    // Requires DevicePipelineFeatures::triangle_fans.
    kTriangleFan,
    kLineListWithAdjacency,
    kPatchList,
  };

  enum class PipelinePolygonMode : uint32_t {
    kFill,
    kLine,
    kPoint,
  };

  XEPACKEDSTRUCT(PipelineDescription, {
    uint64_t vertex_shader_hash;
    uint64_t vertex_shader_modification;
    // 0 if no pixel shader.
    uint64_t pixel_shader_hash;
    uint64_t pixel_shader_modification;
    VulkanRenderTargetCache::RenderPassKey render_pass_key;

    // Input assembly.
    PipelinePrimitiveTopology primitive_topology : 3;  // 3
    uint32_t primitive_restart : 1;                    // 4
    // Rasterization.
    PipelinePolygonMode polygon_mode : 2;  // 6
    uint32_t cull_front : 1;               // 7
    uint32_t cull_back : 1;                // 8
    uint32_t front_face_clockwise : 1;     // 9

    // Including all the padding, for a stable hash.
    PipelineDescription() { Reset(); }
    PipelineDescription(const PipelineDescription& description) {
      std::memcpy(this, &description, sizeof(*this));
    }
    PipelineDescription& operator=(const PipelineDescription& description) {
      std::memcpy(this, &description, sizeof(*this));
      return *this;
    }
    bool operator==(const PipelineDescription& description) const {
      return std::memcmp(this, &description, sizeof(*this)) == 0;
    }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
    uint64_t GetHash() const { return XXH3_64bits(this, sizeof(*this)); }
    struct Hasher {
      size_t operator()(const PipelineDescription& description) const {
        return size_t(description.GetHash());
      }
    };
  });

  struct Pipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    // Owned by VulkanCommandProcessor, valid until ClearCache.
    const PipelineLayoutProvider* pipeline_layout;
    Pipeline(const PipelineLayoutProvider* pipeline_layout_provider)
        : pipeline_layout(pipeline_layout_provider) {}
  };

  // Description that can be passed from the command processor thread to the
  // creation threads, with everything needed from caches pre-looked-up.
  struct PipelineCreationArguments {
    std::pair<const PipelineDescription, Pipeline>* pipeline;
    const VulkanShader::VulkanTranslation* vertex_shader;
    const VulkanShader::VulkanTranslation* pixel_shader;
    VkRenderPass render_pass;
  };

  // Can be called from multiple threads.
  bool TranslateAnalyzedShader(SpirvShaderTranslator& translator,
                               VulkanShader::VulkanTranslation& translation);

  bool GetCurrentStateDescription(
      const VulkanShader::VulkanTranslation* vertex_shader,
      const VulkanShader::VulkanTranslation* pixel_shader,
      VulkanRenderTargetCache::RenderPassKey render_pass_key,
      PipelineDescription& description_out) const;

  // Can be called from creation threads - all needed data must be fully set up
  // at the point of the call: shaders must be translated, pipeline layout and
  // render pass objects must be available.
  bool EnsurePipelineCreated(
      const PipelineCreationArguments& creation_arguments);

  VulkanCommandProcessor& command_processor_;
  const RegisterFile& register_file_;
  VulkanRenderTargetCache& render_target_cache_;

  DevicePipelineFeatures device_pipeline_features_;

  // Temporary storage for AnalyzeUcode calls on the processor thread.
  StringBuffer ucode_disasm_buffer_;
  // Reusable shader translator on the command processor thread.
  std::unique_ptr<SpirvShaderTranslator> shader_translator_;

  // Ucode hash -> shader.
  std::unordered_map<uint64_t, VulkanShader*,
                     xe::hash::IdentityHasher<uint64_t>>
      shaders_;

  std::unordered_map<PipelineDescription, Pipeline, PipelineDescription::Hasher>
      pipelines_;

  // Previously used pipeline, to avoid lookups if the state wasn't changed.
  const std::pair<const PipelineDescription, Pipeline>* last_pipeline_ =
      nullptr;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_PIPELINE_STATE_CACHE_H_
