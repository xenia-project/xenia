/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GRAPHICS_PROVIDER_H_
#define XENIA_UI_GRAPHICS_PROVIDER_H_

#include <memory>

#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/presenter.h"

namespace xe {
namespace ui {

class Window;

// Factory for graphics contexts.
// All contexts created by the same provider will be able to share resources
// according to the rules of the backing graphics API.
class GraphicsProvider {
 public:
  enum class GpuVendorID {
    kAMD = 0x1002,
    kApple = 0x106B,
    kArm = 0x13B5,
    kImagination = 0x1010,
    kIntel = 0x8086,
    kMicrosoft = 0x1414,
    kNvidia = 0x10DE,
    kQualcomm = 0x5143,
  };

  virtual ~GraphicsProvider() = default;

  // It's safe to reinitialize the presenter in the host GPU loss callback if it
  // was called from the UI thread as specified in the arguments.
  virtual std::unique_ptr<Presenter> CreatePresenter(
      Presenter::HostGpuLossCallback host_gpu_loss_callback =
          Presenter::FatalErrorHostGpuLossCallback) = 0;

  virtual std::unique_ptr<ImmediateDrawer> CreateImmediateDrawer() = 0;

 protected:
  GraphicsProvider() = default;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GRAPHICS_PROVIDER_H_
