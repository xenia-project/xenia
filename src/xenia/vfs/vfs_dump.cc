/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
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

namespace xe {
namespace vfs {

using namespace xe::literals;

DEFINE_transient_path(source, "", "Specifies the file to dump from.",
                      "General");

DEFINE_transient_path(dump_path, "",
                      "Specifies the directory to dump files to.", "General");

int vfs_dump_main(const std::vector<std::string>& args) {
  if (cvars::source.empty()) {
    XELOGE("Usage: {} [source] [dump_path]", xe::path_to_utf8(args[0]));
    return 1;
  }

  std::filesystem::path base_path = cvars::dump_path.empty() ? cvars::source : cvars::dump_path;
  std::unique_ptr<vfs::Device> device;

  // TODO: Flags specifying the type of device.
  device = std::make_unique<vfs::StfsContainerDevice>("", cvars::source);
  if (!device->Initialize()) {
    XELOGE("Failed to initialize device");
    return 1;
  }

  // Run through all the files, breadth-first style.
  std::queue<vfs::Entry*> queue;
  auto root = device->ResolvePath("/");
  queue.push(root);

  // Allocate a buffer when needed.
  size_t buffer_size = 0;
  uint8_t* buffer = nullptr;

  while (!queue.empty()) {
    auto entry = queue.front();
    queue.pop();
    for (auto& entry : entry->children()) {
      queue.push(entry.get());
    }

    XELOGI("{}", entry->path());
    auto dest_name = base_path / xe::to_path(entry->path());
    if (entry->attributes() & kFileAttributeDirectory) {
      std::filesystem::create_directories(dest_name);
      continue;
    }

    vfs::File* in_file = nullptr;
    if (entry->Open(FileAccess::kFileReadData, &in_file) != X_STATUS_SUCCESS) {
      continue;
    }

    auto file = xe::filesystem::OpenFile(dest_name, "wb");
    if (!file) {
      in_file->Destroy();
      continue;
    }

    if (entry->can_map()) {
      auto map = entry->OpenMapped(xe::MappedMemory::Mode::kRead);
      fwrite(map->data(), map->size(), 1, file);
      map->Close();
    } else {
      // Can't map the file into memory. Read it into a temporary buffer.
      if (!buffer || entry->size() > buffer_size) {
        // Resize the buffer.
        if (buffer) {
          delete[] buffer;
        }

        // Allocate a buffer rounded up to the nearest 512MB.
        buffer_size = xe::round_up(entry->size(), 512_MiB);
        buffer = new uint8_t[buffer_size];
      }

      size_t bytes_read = 0;
      in_file->ReadSync(buffer, entry->size(), 0, &bytes_read);
      fwrite(buffer, bytes_read, 1, file);
    }

    fclose(file);
    in_file->Destroy();
  }

  if (buffer) {
    delete[] buffer;
  }

  return 0;
}

}  // namespace vfs
}  // namespace xe

XE_DEFINE_CONSOLE_APP("xenia-vfs-dump", xe::vfs::vfs_dump_main,
                      "[source] [dump_path]", "source", "dump_path");
