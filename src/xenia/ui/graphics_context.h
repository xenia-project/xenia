/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GRAPHICS_CONTEXT_H_
#define XENIA_UI_GRAPHICS_CONTEXT_H_

#include <memory>

#include "el/graphics/renderer.h"
#include "xenia/profiling.h"

namespace xe {
namespace ui {

class Window;

class GraphicsContext {
 public:
  virtual ~GraphicsContext();

  Window* target_window() const { return target_window_; }

  virtual std::unique_ptr<GraphicsContext> CreateShared() = 0;
  virtual std::unique_ptr<ProfilerDisplay> CreateProfilerDisplay() = 0;
  virtual std::unique_ptr<el::graphics::Renderer> CreateElementalRenderer() = 0;

  virtual bool MakeCurrent() = 0;
  virtual void ClearCurrent() = 0;

  virtual void BeginSwap() = 0;
  virtual void EndSwap() = 0;

 protected:
  explicit GraphicsContext(Window* target_window);

  Window* target_window_ = nullptr;
};

struct GraphicsContextLock {
  GraphicsContextLock(GraphicsContext* context) : context_(context) {
    context_->MakeCurrent();
  }
  ~GraphicsContextLock() { context_->ClearCurrent(); }

 private:
  GraphicsContext* context_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GRAPHICS_CONTEXT_H_
