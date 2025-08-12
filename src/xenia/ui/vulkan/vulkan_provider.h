/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_PROVIDER_H_
#define XENIA_UI_VULKAN_VULKAN_PROVIDER_H_

#include <memory>

#include "xenia/ui/graphics_provider.h"
#include "xenia/ui/vulkan/ui_samplers.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_instance.h"

namespace xe {
namespace ui {
namespace vulkan {

class VulkanProvider : public GraphicsProvider {
 public:
  static std::unique_ptr<VulkanProvider> Create(bool with_gpu_emulation,
                                                bool with_presentation);

  VulkanInstance* vulkan_instance() const { return vulkan_instance_.get(); }

  VulkanDevice* vulkan_device() const { return vulkan_device_.get(); }

  // nullptr if created without presentation support.
  const UISamplers* ui_samplers() const { return ui_samplers_.get(); }

  std::unique_ptr<Presenter> CreatePresenter(
      Presenter::HostGpuLossCallback host_gpu_loss_callback =
          Presenter::FatalErrorHostGpuLossCallback) override;

  std::unique_ptr<ImmediateDrawer> CreateImmediateDrawer() override;

 private:
  explicit VulkanProvider() = default;

  std::unique_ptr<VulkanInstance> vulkan_instance_;

  // Depends on the instance.
  std::unique_ptr<VulkanDevice> vulkan_device_;

  // Depends on the device.
  std::unique_ptr<UISamplers> ui_samplers_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_PROVIDER_H_
