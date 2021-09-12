/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOWED_APP_CONTEXT_GTK_H_
#define XENIA_UI_WINDOWED_APP_CONTEXT_GTK_H_

#include <glib.h>
#include <mutex>

#include "xenia/ui/windowed_app_context.h"

namespace xe {
namespace ui {

class GTKWindowedAppContext final : public WindowedAppContext {
 public:
  GTKWindowedAppContext() = default;
  ~GTKWindowedAppContext();

  void NotifyUILoopOfPendingFunctions() override;

  void PlatformQuitFromUIThread() override;

  void RunMainGTKLoop();

 private:
  static gboolean PendingFunctionsSourceFunc(gpointer data);

  static gboolean QuitSourceFunc(gpointer data);

  std::mutex pending_functions_idle_pending_mutex_;
  guint pending_functions_idle_pending_ = 0;

  guint quit_idle_pending_ = 0;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOWED_APP_CONTEXT_GTK_H_
