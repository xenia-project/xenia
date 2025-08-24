/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <queue>
#include <string>
#include <vector>

#include "xenia/base/console_app_main.h"
#include "xenia/base/cvar.h"
#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"

#include "xenia/vfs/devices/stfs_container_device.h"
#include "xenia/vfs/file.h"
#include "xenia/vfs/virtual_file_system.h"

namespace xe {
namespace vfs {

using namespace xe::literals;

DEFINE_transient_path(source, "", "Specifies the file to dump from.",
                      "General");

DEFINE_transient_path(dump_path, "",
                      "Specifies the directory to dump files to.", "General");

int vfs_dump_main(const std::vector<std::string>& args) {
  if (cvars::source.empty() || cvars::dump_path.empty()) {
    XELOGE("Usage: {} [source] [dump_path]", xe::path_to_utf8(args[0]));
    return 1;
  }

  std::filesystem::path base_path = cvars::dump_path;
  std::unique_ptr<vfs::Device> device;

  // TODO: Flags specifying the type of device.
  device = std::make_unique<vfs::StfsContainerDevice>("", cvars::source);
  if (!device->Initialize()) {
    XELOGE("Failed to initialize device");
    return 1;
  }
  return VirtualFileSystem::ExtractFiles(device.get(), base_path);
}

}  // namespace vfs
}  // namespace xe

XE_DEFINE_CONSOLE_APP("xenia-vfs-dump", xe::vfs::vfs_dump_main,
                      "[source] [dump_path]", "source", "dump_path");
