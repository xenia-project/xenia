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

namespace xe {
namespace ui {

class ImmediateDrawer;
class Window;

class GraphicsContext {
 public:
  virtual ~GraphicsContext();

  Window* target_window() const { return target_window_; }

  virtual std::unique_ptr<GraphicsContext> CreateShared() = 0;

  virtual ImmediateDrawer* immediate_drawer() = 0;

  virtual bool is_current() = 0;
  virtual bool MakeCurrent() = 0;
  virtual void ClearCurrent() = 0;

  virtual void BeginSwap() = 0;
  virtual void EndSwap() = 0;

 protected:
  explicit GraphicsContext(Window* target_window);

  Window* target_window_ = nullptr;
};

struct GraphicsContextLock {
  explicit GraphicsContextLock(GraphicsContext* context) : context_(context) {
    was_current_ = context_->is_current();
    if (!was_current_) {
      context_->MakeCurrent();
    }
  }
  ~GraphicsContextLock() {
    if (!was_current_) {
      context_->ClearCurrent();
    }
  }

 private:
  bool was_current_ = false;
  GraphicsContext* context_ = nullptr;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GRAPHICS_CONTEXT_H_
