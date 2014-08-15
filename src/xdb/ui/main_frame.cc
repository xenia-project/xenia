/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xdb/ui/main_frame.h>

namespace xdb {
namespace ui {

MainFrame::MainFrame(std::unique_ptr<DebugTarget> debug_target)
    : MainFrameBase(nullptr), debug_target_(std::move(debug_target)) {
  cursor_ = std::move(debug_target_->CreateCursor());
  cursor_->end_of_stream.AddListener([]() {
    XELOGI("eos");
  });
  cursor_->module_loaded.AddListener([](Module* module) {
    //
    XELOGI("mod load");
  });
  cursor_->module_unloaded.AddListener([](Module* module) {
    //
    XELOGI("mod unload");
  });
  cursor_->thread_created.AddListener([](Thread* thread) {
    //
    XELOGI("thread create");
  });
  cursor_->thread_exited.AddListener([](Thread* thread) {
    //
    XELOGI("thread exit");
  });
  cursor_->Continue();
}

void MainFrame::OnIdle(wxIdleEvent& event) {
}

}  // namespace ui
}  // namespace xdb
