/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_provider.h"

#include "xenia/base/logging.h"
#include "xenia/ui/vulkan/vulkan_context.h"

namespace xe {
namespace ui {
namespace vulkan {

std::unique_ptr<VulkanProvider> VulkanProvider::Create(Window* main_window) {
  std::unique_ptr<VulkanProvider> provider(new VulkanProvider(main_window));
  if (!provider->Initialize()) {
    xe::FatalError(
        "Unable to initialize Direct3D 12 graphics subsystem.\n"
        "\n"
        "Ensure that you have the latest drivers for your GPU and it supports "
        "Vulkan, and that you have the latest Vulkan runtime installed, which "
        "can be downloaded at https://vulkan.lunarg.com/sdk/home.\n"
        "\n"
        "See https://xenia.jp/faq/ for more information and a list of "
        "supported GPUs.");
    return nullptr;
  }
  return provider;
}

VulkanProvider::VulkanProvider(Window* main_window)
    : GraphicsProvider(main_window) {}

bool VulkanProvider::Initialize() { return false; }

std::unique_ptr<GraphicsContext> VulkanProvider::CreateContext(
    Window* target_window) {
  auto new_context =
      std::unique_ptr<VulkanContext>(new VulkanContext(this, target_window));
  if (!new_context->Initialize()) {
    return nullptr;
  }
  return std::unique_ptr<GraphicsContext>(new_context.release());
}

std::unique_ptr<GraphicsContext> VulkanProvider::CreateOffscreenContext() {
  auto new_context =
      std::unique_ptr<VulkanContext>(new VulkanContext(this, nullptr));
  if (!new_context->Initialize()) {
    return nullptr;
  }
  return std::unique_ptr<GraphicsContext>(new_context.release());
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
