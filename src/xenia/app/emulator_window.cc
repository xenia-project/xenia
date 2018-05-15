/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <gflags/gflags.h>

#include "xenia/app/emulator_window.h"
#include "xenia/apu/xaudio2/xaudio2_audio_system.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"

#include "xenia/ui/vulkan/vulkan_instance.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

#include <QVulkanWindow>

DEFINE_string(apu, "any", "Audio system. Use: [any, nop, xaudio2]");
DEFINE_string(gpu, "any", "Graphics system. Use: [any, vulkan, null]");
DEFINE_string(hid, "any", "Input system. Use: [any, nop, winkey, xinput]");

DEFINE_string(target, "", "Specifies the target .xex or .iso to execute.");
DEFINE_bool(fullscreen, false, "Toggles fullscreen");

namespace xe {
namespace app {

class VulkanWindow : public QVulkanWindow {
 public:
  VulkanWindow(EmulatorWindow* parent) : window_(parent) {}
  QVulkanWindowRenderer* createRenderer() override;

 private:
  EmulatorWindow* window_;
};

class VulkanRenderer : public QVulkanWindowRenderer {
 public:
  VulkanRenderer(VulkanWindow* window, xe::Emulator* emulator)
      : window_(window), emulator_(emulator) {}

  void startNextFrame() override {
    // Copy the graphics frontbuffer to our backbuffer.
    auto swap_state = emulator_->graphics_system()->swap_state();

    auto cmd = window_->currentCommandBuffer();
    auto src = reinterpret_cast<VkImage>(swap_state->front_buffer_texture);
    auto dest = window_->swapChainImage(window_->currentSwapChainImageIndex());
    auto dest_size = window_->swapChainImageSize();

    VkImageMemoryBarrier barrier;
    std::memset(&barrier, 0, sizeof(VkImageMemoryBarrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = src;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    VkImageBlit region;
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[0] = {0, 0, 0};
    region.srcOffsets[1] = {static_cast<int32_t>(swap_state->width),
                            static_cast<int32_t>(swap_state->height), 1};

    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[0] = {0, 0, 0};
    region.dstOffsets[1] = {static_cast<int32_t>(dest_size.width()),
                            static_cast<int32_t>(dest_size.height()), 1};
    vkCmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_GENERAL, dest,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                   VK_FILTER_LINEAR);

    swap_state->pending = false;
    window_->frameReady();
  }

 private:
  xe::Emulator* emulator_;
  VulkanWindow* window_;
};

QVulkanWindowRenderer* VulkanWindow::createRenderer() {
  return new VulkanRenderer(this, window_->emulator());
}

EmulatorWindow::EmulatorWindow() {}

bool EmulatorWindow::Setup() {
  // TODO(DrChat): Pass in command line arguments.
  emulator_ = std::make_unique<xe::Emulator>(L"");

  // Initialize the graphics backend.
  // TODO(DrChat): Pick from gpu command line flag.
  if (!InitializeVulkan()) {
    return false;
  }

  auto audio_factory = [&](cpu::Processor* processor,
                           kernel::KernelState* kernel_state) {
    auto audio = apu::xaudio2::XAudio2AudioSystem::Create(processor);
    if (audio->Setup(kernel_state) != X_STATUS_SUCCESS) {
      audio->Shutdown();
      return std::unique_ptr<apu::AudioSystem>(nullptr);
    }

    return audio;
  };

  auto graphics_factory = [&](cpu::Processor* processor,
                              kernel::KernelState* kernel_state) {
    auto graphics = std::make_unique<gpu::vulkan::VulkanGraphicsSystem>();
    if (graphics->Setup(processor, kernel_state,
                        graphics_provider_->CreateOffscreenContext())) {
      return std::unique_ptr<gpu::vulkan::VulkanGraphicsSystem>(nullptr);
    }

    return graphics;
  };

  X_STATUS result = emulator_->Setup(audio_factory, graphics_factory, nullptr);
  if (result == X_STATUS_SUCCESS) {
    // Setup a callback called when the emulator wants to swap.
    emulator_->graphics_system()->SetSwapCallback([&]() {
      QMetaObject::invokeMethod(this->graphics_window_.get(), "requestUpdate",
                                Qt::QueuedConnection);
    });
  }

  return result == X_STATUS_SUCCESS;
}

bool EmulatorWindow::InitializeVulkan() {
  auto provider = xe::ui::vulkan::VulkanProvider::Create(nullptr);
  auto device = provider->device();

  // Create a Qt wrapper around our vulkan instance.
  vulkan_instance_ = std::make_unique<QVulkanInstance>();
  vulkan_instance_->setVkInstance(*provider->instance());
  if (!vulkan_instance_->create()) {
    return false;
  }

  graphics_window_ = std::make_unique<VulkanWindow>(this);
  graphics_window_->setVulkanInstance(vulkan_instance_.get());

  // Now set the graphics window as our central widget.
  QWidget* wrapper = QWidget::createWindowContainer(graphics_window_.get());
  setCentralWidget(wrapper);

  graphics_provider_ = std::move(provider);
  return true;
}

void EmulatorWindow::SwapVulkan() {}

bool EmulatorWindow::Launch(const std::wstring& path) {
  return emulator_->LaunchPath(path) == X_STATUS_SUCCESS;
}

}  // namespace app
}  // namespace xe