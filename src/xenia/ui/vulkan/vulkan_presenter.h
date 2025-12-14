/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_PRESENTER_H_
#define XENIA_UI_VULKAN_VULKAN_PRESENTER_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/surface.h"
#include "xenia/ui/vulkan/ui_samplers.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_gpu_completion_timeline.h"
#include "xenia/ui/vulkan/vulkan_instance.h"

namespace xe {
namespace ui {
namespace vulkan {

class VulkanUIDrawContext final : public UIDrawContext {
 public:
  VulkanUIDrawContext(Presenter& presenter, uint32_t render_target_width,
                      uint32_t render_target_height,
                      VkCommandBuffer draw_command_buffer,
                      uint64_t submission_index_current,
                      uint64_t submission_index_completed,
                      VkRenderPass render_pass, VkFormat render_pass_format)
      : UIDrawContext(presenter, render_target_width, render_target_height),
        draw_command_buffer_(draw_command_buffer),
        submission_index_current_(submission_index_current),
        submission_index_completed_(submission_index_completed),
        render_pass_(render_pass),
        render_pass_format_(render_pass_format) {}

  VkCommandBuffer draw_command_buffer() const { return draw_command_buffer_; }
  uint64_t submission_index_current() const {
    return submission_index_current_;
  }
  uint64_t submission_index_completed() const {
    return submission_index_completed_;
  }
  VkRenderPass render_pass() const { return render_pass_; }
  VkFormat render_pass_format() const { return render_pass_format_; }

 private:
  VkCommandBuffer draw_command_buffer_;
  uint64_t submission_index_current_;
  uint64_t submission_index_completed_;
  // Has 1 subpass with a single render_pass_format_ attachment.
  VkRenderPass render_pass_;
  VkFormat render_pass_format_;
};

class VulkanPresenter final : public Presenter {
 public:
  // Maximum number of different guest output image versions still potentially
  // considered alive that may be given to the refresher - this many instances
  // of dependent objects (such as framebuffers) may need to be kept by the
  // refresher across invocations (due to multiple-buffering of guest output
  // images inside the presenter, different versions may be given even every
  // invocation), to avoid recreation of dependent objects every frame.
  static constexpr size_t kMaxActiveGuestOutputImageVersions =
      kGuestOutputMailboxSize;

  static constexpr VkFormat kGuestOutputFormat =
      VK_FORMAT_A2B10G10R10_UNORM_PACK32;
  // The guest output is expected to be acquired and released in this state by
  // the refresher. The exception is the first write to the current guest output
  // image - in this case, a barrier is only needed from
  // VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT without access. Also if the image is
  // being refreshed for the first time, it's in VK_IMAGE_LAYOUT_UNDEFINED (but
  // it's safe, and preferred, to transition it from VK_IMAGE_LAYOUT_UNDEFINED
  // when writing to it in general).
  static constexpr VkPipelineStageFlagBits kGuestOutputInternalStageMask =
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  static constexpr VkAccessFlags kGuestOutputInternalAccessMask =
      VK_ACCESS_SHADER_READ_BIT;
  static constexpr VkImageLayout kGuestOutputInternalLayout =
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  // The callback must use the graphics and compute queue 0 of the device.
  class VulkanGuestOutputRefreshContext final
      : public GuestOutputRefreshContext {
   public:
    VulkanGuestOutputRefreshContext(bool& is_8bpc_out_ref, VkImage image,
                                    VkImageView image_view,
                                    uint64_t image_version,
                                    bool image_ever_written_previously)
        : GuestOutputRefreshContext(is_8bpc_out_ref),
          image_(image),
          image_view_(image_view),
          image_version_(image_version),
          image_ever_written_previously_(image_ever_written_previously) {}

    // The format is kGuestOutputFormat.
    // Supports usage as a color attachment and as a sampled image, as well as
    // transfer source (but the reason of that is guest output capturing).
    VkImage image() const { return image_; }
    VkImageView image_view() const { return image_view_; }
    uint64_t image_version() const { return image_version_; }
    // Whether a proper barrier must be done to acquire the image.
    bool image_ever_written_previously() const {
      return image_ever_written_previously_;
    }

   private:
    VkImage image_;
    VkImageView image_view_;
    uint64_t image_version_;
    bool image_ever_written_previously_;
  };

  static std::unique_ptr<VulkanPresenter> Create(
      HostGpuLossCallback host_gpu_loss_callback, VulkanDevice* vulkan_device,
      const UISamplers* ui_samplers) {
    auto presenter = std::unique_ptr<VulkanPresenter>(new VulkanPresenter(
        host_gpu_loss_callback, vulkan_device, ui_samplers));
    if (!presenter->InitializeSurfaceIndependent()) {
      return nullptr;
    }
    return presenter;
  }

  ~VulkanPresenter();

  VulkanDevice* vulkan_device() const { return vulkan_device_; }

  static Surface::TypeFlags GetSurfaceTypesSupportedByInstance(
      const VulkanInstance::Extensions& instance_extensions);
  Surface::TypeFlags GetSupportedSurfaceTypes() const override;

  bool CaptureGuestOutput(RawImage& image_out) override;

  void AwaitUISubmissionCompletionFromUIThread(uint64_t submission_index) {
    ui_completion_timeline_.AwaitSubmissionAndUpdateCompleted(submission_index);
  }
  VkCommandBuffer AcquireUISetupCommandBufferFromUIThread();

 protected:
  SurfacePaintConnectResult ConnectOrReconnectPaintingToSurfaceFromUIThread(
      Surface& new_surface, uint32_t new_surface_width,
      uint32_t new_surface_height, bool was_paintable,
      bool& is_vsync_implicit_out) override;
  void DisconnectPaintingFromSurfaceFromUIThreadImpl() override;

  bool RefreshGuestOutputImpl(
      uint32_t mailbox_index, uint32_t frontbuffer_width,
      uint32_t frontbuffer_height,
      std::function<bool(GuestOutputRefreshContext& context)> refresher,
      bool& is_8bpc_out_ref) override;

  PaintResult PaintAndPresentImpl(bool execute_ui_drawers) override;

 private:
  // Usable for both the guest output image itself and for intermediate images.
  class GuestOutputImage {
   public:
    static std::unique_ptr<GuestOutputImage> Create(
        const VulkanDevice* const vulkan_device, const uint32_t width,
        const uint32_t height) {
      assert_not_zero(width);
      assert_not_zero(height);
      auto image = std::unique_ptr<GuestOutputImage>(
          new GuestOutputImage(vulkan_device, width, height));
      if (!image->Initialize()) {
        return nullptr;
      }
      return std::move(image);
    }

    GuestOutputImage(const GuestOutputImage& image) = delete;
    GuestOutputImage& operator=(const GuestOutputImage& image) = delete;
    ~GuestOutputImage();

    const VkExtent2D& extent() const { return extent_; }

    VkImage image() const { return image_; }
    VkDeviceMemory memory() const { return memory_; }
    VkImageView view() const { return view_; }

   private:
    GuestOutputImage(const VulkanDevice* const vulkan_device,
                     const uint32_t width, const uint32_t height)
        : vulkan_device_(vulkan_device) {
      extent_.width = width;
      extent_.height = height;
    }

    bool Initialize();

    const VulkanDevice* vulkan_device_;

    VkExtent2D extent_;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
  };

  struct GuestOutputImageInstance {
    // Refresher-side reference (painting has its own references for the purpose
    // of destruction only after painting is done on the GPU).
    std::shared_ptr<GuestOutputImage> image;
    uint64_t version = UINT64_MAX;
    uint64_t last_refresher_submission = 0;
    // For choosing the barrier stage and access mask and layout depending on
    // whether the image has previously been written. If an image is active
    // after a refresh, it can be assumed that this is true.
    bool ever_successfully_refreshed = false;

    void SetToNewImage(const std::shared_ptr<GuestOutputImage>& new_image,
                       uint64_t new_version) {
      image = new_image;
      version = new_version;
      last_refresher_submission = 0;
      ever_successfully_refreshed = false;
    }
  };

  struct GuestOutputPaintRectangleConstants {
    union {
      struct {
        float x;
        float y;
      };
      float offset[2];
    };
    union {
      struct {
        float width;
        float height;
      };
      float size[2];
    };
  };

  enum GuestOutputPaintPipelineLayoutIndex : size_t {
    kGuestOutputPaintPipelineLayoutIndexBilinear,
    kGuestOutputPaintPipelineLayoutIndexCasSharpen,
    kGuestOutputPaintPipelineLayoutIndexCasResample,
    kGuestOutputPaintPipelineLayoutIndexFsrEasu,
    kGuestOutputPaintPipelineLayoutIndexFsrRcas,

    kGuestOutputPaintPipelineLayoutCount,
  };

  static constexpr GuestOutputPaintPipelineLayoutIndex
  GetGuestOutputPaintPipelineLayoutIndex(GuestOutputPaintEffect effect) {
    switch (effect) {
      case GuestOutputPaintEffect::kBilinear:
      case GuestOutputPaintEffect::kBilinearDither:
        return kGuestOutputPaintPipelineLayoutIndexBilinear;
      case GuestOutputPaintEffect::kCasSharpen:
      case GuestOutputPaintEffect::kCasSharpenDither:
        return kGuestOutputPaintPipelineLayoutIndexCasSharpen;
      case GuestOutputPaintEffect::kCasResample:
      case GuestOutputPaintEffect::kCasResampleDither:
        return kGuestOutputPaintPipelineLayoutIndexCasResample;
      case GuestOutputPaintEffect::kFsrEasu:
        return kGuestOutputPaintPipelineLayoutIndexFsrEasu;
      case GuestOutputPaintEffect::kFsrRcas:
      case GuestOutputPaintEffect::kFsrRcasDither:
        return kGuestOutputPaintPipelineLayoutIndexFsrRcas;
      default:
        assert_unhandled_case(effect);
        return kGuestOutputPaintPipelineLayoutCount;
    }
  }

  struct PaintContext {
    class Submission {
     public:
      static std::unique_ptr<Submission> Create(
          const VulkanDevice* const vulkan_device) {
        auto submission =
            std::unique_ptr<Submission>(new Submission(vulkan_device));
        if (!submission->Initialize()) {
          return nullptr;
        }
        return submission;
      }

      Submission(const Submission& submission) = delete;
      Submission& operator=(const Submission& submission) = delete;
      ~Submission();

      VkSemaphore acquire_semaphore() const { return acquire_semaphore_; }
      VkSemaphore present_semaphore() const { return present_semaphore_; }
      VkCommandPool draw_command_pool() const { return draw_command_pool_; }
      VkCommandBuffer draw_command_buffer() const {
        return draw_command_buffer_;
      }

     private:
      explicit Submission(const VulkanDevice* const vulkan_device)
          : vulkan_device_(vulkan_device) {}
      bool Initialize();

      const VulkanDevice* vulkan_device_;
      VkSemaphore acquire_semaphore_ = VK_NULL_HANDLE;
      VkSemaphore present_semaphore_ = VK_NULL_HANDLE;
      VkCommandPool draw_command_pool_ = VK_NULL_HANDLE;
      VkCommandBuffer draw_command_buffer_ = VK_NULL_HANDLE;
    };

    static constexpr uint32_t kSubmissionCount = 3;

    struct GuestOutputPaintPipeline {
      // Created on initialization.
      VkPipeline intermediate_pipeline = VK_NULL_HANDLE;
      // Created during guest output painting (after awaiting the last guest
      // output paint if outdated and needs to be recreated), when needed, for
      // the up-to-date render pass that draws to the swapchain with the actual
      // image format.
      VkPipeline swapchain_pipeline = VK_NULL_HANDLE;
      VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    };

    enum GuestOutputDescriptorSet : uint32_t {
      kGuestOutputDescriptorSetGuestOutput0Sampled,

      kGuestOutputDescriptorSetIntermediate0Sampled =
          kGuestOutputDescriptorSetGuestOutput0Sampled +
          kGuestOutputMailboxSize,

      kGuestOutputDescriptorSetCount =
          kGuestOutputDescriptorSetIntermediate0Sampled +
          kMaxGuestOutputPaintEffects - 1,
    };

    struct UISetupCommandBuffer {
      UISetupCommandBuffer(VkCommandPool command_pool,
                           VkCommandBuffer command_buffer,
                           uint64_t last_usage_submission_index = 0)
          : command_pool(command_pool),
            command_buffer(command_buffer),
            last_usage_submission_index(last_usage_submission_index) {}

      VkCommandPool command_pool;
      VkCommandBuffer command_buffer;
      uint64_t last_usage_submission_index;
    };

    struct SwapchainFramebuffer {
      SwapchainFramebuffer(VkImageView image_view, VkFramebuffer framebuffer)
          : image_view(image_view), framebuffer(framebuffer) {}

      VkImageView image_view;
      VkFramebuffer framebuffer;
    };

    explicit PaintContext(VulkanDevice* const vulkan_device)
        : vulkan_device(vulkan_device), completion_timeline(vulkan_device) {}
    PaintContext(const PaintContext& paint_context) = delete;
    PaintContext& operator=(const PaintContext& paint_context) = delete;

    // The old swapchain, if passed, should be assumed to be retired after this
    // call (though it may fail before the vkCreateSwapchainKHR that will
    // technically retire it, so it will be in an undefined state), and needs to
    // be destroyed externally no matter what the result is.
    static VkSwapchainKHR CreateSwapchainForVulkanSurface(
        const VulkanDevice* vulkan_device, VkSurfaceKHR surface, uint32_t width,
        uint32_t height, VkSwapchainKHR old_swapchain,
        uint32_t& present_queue_family_out, VkFormat& image_format_out,
        VkExtent2D& image_extent_out, bool& is_fifo_out,
        bool& ui_surface_unusable_out);

    // Destroys the swapchain and its derivatives, nulls `swapchain` and returns
    // the original swapchain object, if it existed, for use as oldSwapchain if
    // needed and for destruction.
    VkSwapchainKHR PrepareForSwapchainRetirement();
    // May be called from the destructor of the presenter.
    void DestroySwapchainAndVulkanSurface();

    // Connection-indepedent.

    VulkanDevice* vulkan_device;

    std::array<std::unique_ptr<PaintContext::Submission>, kSubmissionCount>
        submissions;
    VulkanGPUCompletionTimeline completion_timeline;

    std::array<GuestOutputPaintPipeline, size_t(GuestOutputPaintEffect::kCount)>
        guest_output_paint_pipelines;

    VkDescriptorPool guest_output_descriptor_pool = VK_NULL_HANDLE;
    // Descriptors are updated while painting if they're out of date.
    VkDescriptorSet
        guest_output_descriptor_sets[kGuestOutputDescriptorSetCount];

    // Refreshed and cleaned up during guest output painting. The first is the
    // paint submission index in which the guest output image (and its
    // descriptors) was last used, the second is the reference to the image,
    // which may be null. The indices are not mailbox indices here, rather, if
    // the reference is not in this array yet, the most outdated reference, if
    // needed, is replaced with the new one, awaiting the usage completion of
    // the last paint usage.
    std::array<std::pair<uint64_t, std::shared_ptr<GuestOutputImage>>,
               kGuestOutputMailboxSize>
        guest_output_image_paint_refs;
    // The latest submission index at which any guest output image was drawn.
    uint64_t guest_output_image_paint_last_submission = 0;

    // Current intermediate images for guest output painting, refreshed when
    // painting guest output.
    std::array<std::unique_ptr<GuestOutputImage>,
               kMaxGuestOutputPaintEffects - 1>
        guest_output_intermediate_images;
    // Created and destroyed alongside the images. UNORM only.
    std::array<VkFramebuffer, kMaxGuestOutputPaintEffects - 1>
        guest_output_intermediate_framebuffers = {};
    uint64_t guest_output_intermediate_image_last_submission = 0;

    // Command buffers optionally executed before the draw command buffer,
    // outside the painting render pass.
    std::vector<UISetupCommandBuffer> ui_setup_command_buffers;
    size_t ui_setup_command_buffer_current_index = SIZE_MAX;

    // Connection-specific.

    // May be reused between connections if the format stays the same.
    VkRenderPass swapchain_render_pass = VK_NULL_HANDLE;
    VkFormat swapchain_render_pass_format = VK_FORMAT_UNDEFINED;
    bool swapchain_render_pass_clear_load_op = false;

    VkSurfaceKHR vulkan_surface = VK_NULL_HANDLE;
    uint32_t present_queue_family = UINT32_MAX;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D swapchain_extent = {};
    bool swapchain_is_fifo = false;
    std::vector<VkImage> swapchain_images;
    std::vector<SwapchainFramebuffer> swapchain_framebuffers;
  };

  explicit VulkanPresenter(HostGpuLossCallback host_gpu_loss_callback,
                           VulkanDevice* vulkan_device,
                           const UISamplers* ui_samplers)
      : Presenter(host_gpu_loss_callback),
        vulkan_device_(vulkan_device),
        ui_samplers_(ui_samplers),
        guest_output_image_refresher_completion_timeline_(vulkan_device),
        ui_completion_timeline_(vulkan_device),
        paint_context_(vulkan_device) {
    assert_not_null(vulkan_device);
    assert_not_null(ui_samplers);
  }

  bool InitializeSurfaceIndependent();

  [[nodiscard]] VkPipeline CreateGuestOutputPaintPipeline(
      GuestOutputPaintEffect effect, VkRenderPass render_pass);

  VulkanDevice* vulkan_device_;
  const UISamplers* ui_samplers_;

  // Static objects for guest output presentation, used only when painting the
  // main target (can be destroyed only after awaiting main target usage
  // completion).
  VkDescriptorSetLayout guest_output_paint_image_descriptor_set_layout_ =
      VK_NULL_HANDLE;
  std::array<VkPipelineLayout, kGuestOutputPaintPipelineLayoutCount>
      guest_output_paint_pipeline_layouts_ = {};
  VkShaderModule guest_output_paint_vs_ = VK_NULL_HANDLE;
  std::array<VkShaderModule, size_t(GuestOutputPaintEffect::kCount)>
      guest_output_paint_fs_ = {};
  // Not compatible with the swapchain render pass even if the format is the
  // same due to different dependencies (this is shader read > color
  // attachment > shader read).
  VkRenderPass guest_output_intermediate_render_pass_ = VK_NULL_HANDLE;

  // Value monotonically increased every time a new guest output image is
  // initialized, for recreation of dependent objects such as framebuffers in
  // the refreshers - saving and comparing the handle in the refresher is not
  // enough as Create > Destroy > Create may result in the same handle for
  // actually different objects without the refresher being aware of the
  // destruction.
  uint64_t guest_output_image_next_version_ = 0;
  std::array<GuestOutputImageInstance, kGuestOutputMailboxSize>
      guest_output_images_;
  VulkanGPUCompletionTimeline guest_output_image_refresher_completion_timeline_;

  // UI submission completion timeline with the submission index that can be
  // given to UI drawers (accessible from the UI thread only, at any time).
  VulkanGPUCompletionTimeline ui_completion_timeline_;

  // Accessible only by painting and by surface connection lifetime management
  // (ConnectOrReconnectPaintingToSurfaceFromUIThread,
  // DisconnectPaintingFromSurfaceFromUIThreadImpl) by the thread doing it, as
  // well as by presenter initialization and shutdown.
  PaintContext paint_context_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_PRESENTER_H_
