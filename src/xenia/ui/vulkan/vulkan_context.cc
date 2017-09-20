/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_context.h"

#include <mutex>
#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"
#include "xenia/ui/vulkan/vulkan_instance.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_swap_chain.h"
#include "xenia/ui/vulkan/vulkan_util.h"
#include "xenia/ui/window.h"

#if XE_PLATFORM_LINUX
#include "xenia/ui/window_gtk.h"
#endif

namespace xe {
namespace ui {
namespace vulkan {

VulkanContext::VulkanContext(VulkanProvider* provider, Window* target_window)
    : GraphicsContext(provider, target_window) {}

VulkanContext::~VulkanContext() {
  auto provider = static_cast<VulkanProvider*>(provider_);
  auto device = provider->device();
  {
    std::lock_guard<std::mutex> queue_lock(device->primary_queue_mutex());
    vkQueueWaitIdle(device->primary_queue());
  }
  immediate_drawer_.reset();
  swap_chain_.reset();
}

bool VulkanContext::Initialize() {
  auto provider = static_cast<VulkanProvider*>(provider_);
  auto device = provider->device();

  if (target_window_) {
    // Create swap chain used to present to the window.
    VkSurfaceKHR surface = nullptr;
#if XE_PLATFORM_WIN32
    VkWin32SurfaceCreateInfoKHR create_info;
    create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    create_info.pNext = nullptr;
    create_info.flags = 0;
    create_info.hinstance =
        static_cast<HINSTANCE>(target_window_->native_platform_handle());
    create_info.hwnd = static_cast<HWND>(target_window_->native_handle());
    auto err = vkCreateWin32SurfaceKHR(*provider->instance(), &create_info,
                                       nullptr, &surface);
    CheckResult(err, "vkCreateWin32SurfaceKHR");
#elif XE_PLATFORM_LINUX
#ifdef GDK_WINDOWING_X11
    GtkWidget* window_handle =
        static_cast<GtkWidget*>(target_window_->native_handle());
    GdkDisplay* gdk_display = gtk_widget_get_display(window_handle);
    assert(GDK_IS_X11_DISPLAY(gdk_display));
    xcb_connection_t* connection =
        XGetXCBConnection(gdk_x11_display_get_xdisplay(gdk_display));
    xcb_window_t window =
        gdk_x11_window_get_xid(gtk_widget_get_window(window_handle));
    VkXcbSurfaceCreateInfoKHR create_info;
    create_info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    create_info.pNext = nullptr;
    create_info.flags = 0;
    create_info.connection = static_cast<xcb_connection_t*>(
        target_window_->native_platform_handle());
    create_info.window = static_cast<xcb_window_t>(window);
    auto err = vkCreateXcbSurfaceKHR(*provider->instance(), &create_info,
                                     nullptr, &surface);
    CheckResult(err, "vkCreateXcbSurfaceKHR");
#else
#error Unsupported GDK Backend on Linux.
#endif  // GDK_WINDOWING_X11
#else
#error Platform not yet implemented.
#endif  // XE_PLATFORM_WIN32
    swap_chain_ = std::make_unique<VulkanSwapChain>(provider->instance(),
                                                    provider->device());
    if (!swap_chain_->Initialize(surface)) {
      XELOGE("Unable to initialize swap chain");
      vkDestroySurfaceKHR(*provider->instance(), surface, nullptr);
      return false;
    }

    // Only initialize immediate mode drawer if we are not an offscreen context.
    immediate_drawer_ = std::make_unique<VulkanImmediateDrawer>(this);
  }

  return true;
}

ImmediateDrawer* VulkanContext::immediate_drawer() {
  return immediate_drawer_.get();
}

VulkanInstance* VulkanContext::instance() const {
  return static_cast<VulkanProvider*>(provider_)->instance();
}

VulkanDevice* VulkanContext::device() const {
  return static_cast<VulkanProvider*>(provider_)->device();
}

bool VulkanContext::is_current() { return false; }

bool VulkanContext::MakeCurrent() {
  SCOPE_profile_cpu_f("gpu");
  return true;
}

void VulkanContext::ClearCurrent() {}

void VulkanContext::BeginSwap() {
  SCOPE_profile_cpu_f("gpu");
  auto provider = static_cast<VulkanProvider*>(provider_);
  auto device = provider->device();

  // If we have a window see if it's been resized since we last swapped.
  // If it has been, we'll need to reinitialize the swap chain before we
  // start touching it.
  if (target_window_) {
    if (target_window_->width() != swap_chain_->surface_width() ||
        target_window_->height() != swap_chain_->surface_height()) {
      // Resized!
      swap_chain_->Reinitialize();
    }
  }

  // Acquire the next image and set it up for use.
  swap_chain_->Begin();

  // TODO(benvanik): use a fence instead? May not be possible with target image.
  std::lock_guard<std::mutex> queue_lock(device->primary_queue_mutex());
  auto err = vkQueueWaitIdle(device->primary_queue());
  CheckResult(err, "vkQueueWaitIdle");
}

void VulkanContext::EndSwap() {
  SCOPE_profile_cpu_f("gpu");
  auto provider = static_cast<VulkanProvider*>(provider_);
  auto device = provider->device();

  // Notify the presentation engine the image is ready.
  // The contents must be in a coherent state.
  swap_chain_->End();

  // Wait until the queue is idle.
  // TODO(benvanik): is this required?
  std::lock_guard<std::mutex> queue_lock(device->primary_queue_mutex());
  auto err = vkQueueWaitIdle(device->primary_queue());
  CheckResult(err, "vkQueueWaitIdle");
}

std::unique_ptr<RawImage> VulkanContext::Capture() {
  // TODO(benvanik): read back swap chain front buffer.
  return nullptr;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
