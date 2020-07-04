/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/filesystem.h"
#include "xenia/base/cvar.h"

#include <algorithm>

DEFINE_path(
    storage_root, "",
    "Root path for persistent internal data storage, or empty "
    "to use the path preferred for the OS, such as the documents folder, or "
    "the emulator executable directory if portable.txt is present in it.",
    "Storage");
DEFINE_path(
    content_root, "",
    "Root path for guest content storage (saves, etc.), or empty to use the "
    "content folder under the storage root.",
    "Storage");


namespace xe {
namespace filesystem {

bool CreateParentFolder(const std::filesystem::path& path) {
  if (path.has_parent_path()) {
    auto parent_path = path.parent_path();
    if (!std::filesystem::exists(parent_path)) {
      return std::filesystem::create_directories(parent_path);
    }
  }
  return true;
}

std::filesystem::path XeniaStorageRoot() {
  std::filesystem::path storage_root = cvars::storage_root;
  if (storage_root.empty()) {
    storage_root = xe::filesystem::GetExecutableFolder();
    if (!std::filesystem::exists(storage_root / "portable.txt")) {
      storage_root = xe::filesystem::GetUserFolder();
#if defined(XE_PLATFORM_WIN32) || defined(XE_PLATFORM_LINUX)
      storage_root = storage_root / "Xenia";
#else
#warning Unhandled platform for the data root.
      storage_root = storage_root / "Xenia";
#endif
    }
  }
  return storage_root;
}


}  // namespace filesystem
}  // namespace xe
