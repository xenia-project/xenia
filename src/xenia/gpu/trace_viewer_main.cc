/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <gflags/gflags.h>
#include "poly/main.h"
#include "poly/mapped_memory.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/gpu/tracing.h"
#include "xenia/emulator.h"
#include "xenia/ui/main_window.h"

DEFINE_string(target_trace_file, "", "Specifies the trace file to load.");

namespace xe {
namespace gpu {

int trace_viewer_main(std::vector<std::wstring>& args) {
  // Create the emulator.
  auto emulator = std::make_unique<Emulator>(L"");
  X_STATUS result = emulator->Setup();
  if (XFAILED(result)) {
    XELOGE("Failed to setup emulator: %.8X", result);
    return 1;
  }

  // Grab path from the flag or unnamed argument.
  if (!FLAGS_target_trace_file.empty() || args.size() >= 2) {
    std::wstring path;
    if (!FLAGS_target_trace_file.empty()) {
      // Passed as a named argument.
      // TODO(benvanik): find something better than gflags that supports
      // unicode.
      path = poly::to_wstring(FLAGS_target_trace_file);
    } else {
      // Passed as an unnamed argument.
      path = args[1];
    }
    // Normalize the path and make absolute.
    std::wstring abs_path = poly::to_absolute_path(path);

    // TODO(benvanik): UI? replay control on graphics system?
    auto graphics_system = emulator->graphics_system();
    auto mmap =
        poly::MappedMemory::Open(abs_path, poly::MappedMemory::Mode::kRead);
    auto trace_data = reinterpret_cast<const uint8_t*>(mmap->data());
    auto trace_size = mmap->size();

    auto trace_ptr = trace_data;
    while (trace_ptr < trace_data + trace_size) {
      trace_ptr = graphics_system->PlayTrace(
          trace_ptr, trace_size - (trace_ptr - trace_data),
          GraphicsSystem::TracePlaybackMode::kBreakOnSwap);
    }

    // Wait until we are exited.
    emulator->main_window()->loop()->AwaitQuit();
  }

  emulator.reset();
  return 0;
}

}  // namespace gpu
}  // namespace xe

DEFINE_ENTRY_POINT(L"gpu_trace_viewer", L"gpu_trace_viewer some.trace",
                   xe::gpu::trace_viewer_main);
