/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/platform_win.h"
#include "xenia/base/string.h"
#include "xenia/base/system.h"

namespace xe {

void LaunchWebBrowser(const std::string& url) {
  auto temp = xe::to_utf16(url);
  ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(temp.c_str()),
                nullptr, nullptr, SW_SHOWNORMAL);
}

void LaunchFileExplorer(const std::filesystem::path& url) {
  ShellExecuteW(nullptr, L"explore", url.c_str(), nullptr, nullptr,
                SW_SHOWNORMAL);
}

}  // namespace xe
