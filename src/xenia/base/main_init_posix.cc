/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2023 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <xbyak/xbyak/xbyak_util.h>

#include "xenia/ui/window_gtk.h"

class StartupCpuFeatureCheck {
 public:
  StartupCpuFeatureCheck() {
    Xbyak::util::Cpu cpu;
    const char* error_message = nullptr;
    if (!cpu.has(Xbyak::util::Cpu::tAVX)) {
      error_message =
          "Your CPU does not support AVX, which is required by Xenia. See "
          "the "
          "FAQ for system requirements at https://xenia.jp";
    }
    if (error_message == nullptr) {
      return;
    } else {
      GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT;
      auto dialog =
          gtk_message_dialog_new(nullptr, flags, GTK_MESSAGE_ERROR,
                                 GTK_BUTTONS_CLOSE, "%s", error_message);
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
      exit(1);
    }
  }
};

// This is a hack to get an instance of StartupAvxCheck
// constructed before any initialization code,
// where the AVX check then happens in the constructor.
// Ref:
// https://reviews.llvm.org/D12689#243295
__attribute__((
    init_priority(101))) static StartupCpuFeatureCheck gStartupAvxCheck;