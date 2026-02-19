/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                *
 ******************************************************************************
 * Copyright 2025. All rights reserved.                                       *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_UPDATE_MANAGER_H_
#define XENIA_UI_UPDATE_MANAGER_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace xe {
namespace ui {

class ImGuiDrawer;

struct UpdateInfo {
  std::string version;
  std::string download_url;
  bool update_available;
};

class UpdateManager {
 public:
  UpdateManager();
  ~UpdateManager();

  // Checks for updates asynchronously
  void CheckForUpdatesAsync(std::function<void(const UpdateInfo&)> callback);

  // Downloads and installs an update
  void DownloadAndInstallUpdate(
      const std::string& download_url,
      std::function<void(uint64_t, uint64_t)> progress_callback,
      std::function<void(bool)> completion_callback);

 private:
  std::string GetCurrentVersion();
  UpdateInfo ParseReleaseInfo(const std::string& json_data);
  std::vector<uint8_t> DownloadFile(
      const std::string& url,
      std::function<void(uint64_t, uint64_t)> progress_callback);
  std::vector<uint8_t> DownloadFileWithParsedUrl(
      const std::wstring& host, const std::wstring& path,
      std::function<void(uint64_t, uint64_t)> progress_callback);
  bool ExtractAndReplace(const std::vector<uint8_t>& zip_data);
  void RestartApplication();
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_UPDATE_MANAGER_H_
