/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/system.h"

namespace xe {

void LaunchWebBrowser(const std::string_view url) {
  // TODO(Triang3l): Intent.ACTION_VIEW (need a Java VM for the thread -
  // possibly restrict this to the UI thread).
  assert_always();
}

void LaunchFileExplorer(const std::filesystem::path& path) { assert_always(); }

void ShowSimpleMessageBox(SimpleMessageBoxType type, std::string_view message) {
  // TODO(Triang3l): Likely not needed much at all. ShowSimpleMessageBox is a
  // concept pretty unfriendly to platforms like Android because it's blocking,
  // and because it can be called from threads other than the UI thread. In the
  // normal execution flow, dialogs should preferably be asynchronous, and used
  // only in the UI thread. However, non-blocking messages may be good for error
  // reporting - investigate the usage of Toasts with respect to threads, and
  // aborting the process immediately after showing a Toast. For a Toast, the
  // Java VM for the calling thread is needed.
}

}  // namespace xe
