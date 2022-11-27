/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>

#include "xenia/base/console.h"
#include "xenia/base/platform_win.h"

namespace xe {

// TODO(Triang3l): Set the default depending on the actual subsystem. Currently
// it inhibits message boxes.
static bool has_console_attached_ = false;

bool has_console_attached() { return has_console_attached_; }

static bool has_shell_environment_variable() {
  size_t size = 0;
  // Check if SHELL exists
  // If it doesn't, then we are in a Windows Terminal
  auto error = _wgetenv_s(&size, nullptr, 0, L"SHELL");
  if (error) {
    return false;
  }
  return !!size;
}

void AttachConsole() {
  
bool has_console = ::AttachConsole(ATTACH_PARENT_PROCESS) == TRUE;
#if 0
  if (!has_console || !has_shell_environment_variable()) {
    // We weren't launched from a console, so just return.
    has_console_attached_ = false;
    return;
  }
  #endif
  AllocConsole();

  has_console_attached_ = true;

  auto std_handle = (intptr_t)GetStdHandle(STD_OUTPUT_HANDLE);
  auto con_handle = _open_osfhandle(std_handle, _O_TEXT);
  auto fp = _fdopen(con_handle, "w");
  freopen_s(&fp, "CONOUT$", "w", stdout);

  std_handle = (intptr_t)GetStdHandle(STD_ERROR_HANDLE);
  con_handle = _open_osfhandle(std_handle, _O_TEXT);
  fp = _fdopen(con_handle, "w");
  freopen_s(&fp, "CONOUT$", "w", stderr);
}

}  // namespace xe
