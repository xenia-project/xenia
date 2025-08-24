/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <ShlObj_core.h>

#include "xenia/base/platform_win.h"
#include "xenia/base/string.h"
#include "xenia/base/system.h"

namespace xe {

void LaunchWebBrowser(const std::string_view url) {
  auto wide_url = xe::to_utf16(url);
  ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(wide_url.c_str()),
                nullptr, nullptr, SW_SHOWNORMAL);
}

void LaunchFileExplorer(const std::filesystem::path& url) {
  ShellExecuteW(nullptr, L"explore", url.c_str(), nullptr, nullptr,
                SW_SHOWNORMAL);
}

void ShowSimpleMessageBox(SimpleMessageBoxType type,
                          const std::string_view message) {
  const wchar_t* title;
  std::u16string wide_message = xe::to_utf16(message);
  DWORD type_flags = MB_OK | MB_APPLMODAL | MB_SETFOREGROUND;
  switch (type) {
    default:
    case SimpleMessageBoxType::Help:
      title = L"Xenia Help";
      type_flags |= MB_ICONINFORMATION;
      break;
    case SimpleMessageBoxType::Warning:
      title = L"Xenia Warning";
      type_flags |= MB_ICONWARNING;
      break;
    case SimpleMessageBoxType::Error:
      title = L"Xenia Error";
      type_flags |= MB_ICONERROR;
      break;
  }
  MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(wide_message.c_str()), title,
              type_flags);
}

const std::filesystem::path GetFontPath(const std::string font_name) {
  std::filesystem::path font_path = "";

  PWSTR fonts_dir;
  HRESULT result = SHGetKnownFolderPath(FOLDERID_Fonts, 0, NULL, &fonts_dir);
  if (FAILED(result)) {
    CoTaskMemFree(static_cast<void*>(fonts_dir));
    return "";
  }
  font_path = std::wstring(fonts_dir);
  font_path.append(font_name);

  CoTaskMemFree(static_cast<void*>(fonts_dir));

  if (!std::filesystem::exists(font_path)) {
    return "";
  }

  return font_path;
}

}  // namespace xe
