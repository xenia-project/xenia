/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/tracing.h"

#include "xenia/base/filesystem.h"
#include "xenia/base/string.h"

namespace xe {
namespace gpu {

TraceWriter::TraceWriter(uint8_t* membase)
    : membase_(membase), file_(nullptr) {}

TraceWriter::~TraceWriter() = default;

bool TraceWriter::Open(const std::wstring& path) {
  Close();

  auto canonical_path = xe::to_absolute_path(path);
  auto base_path = xe::find_base_path(canonical_path);
  xe::filesystem::CreateFolder(base_path);

  file_ = xe::filesystem::OpenFile(canonical_path, "wb");
  return file_ != nullptr;
}

void TraceWriter::Flush() {
  if (file_) {
    fflush(file_);
  }
}

void TraceWriter::Close() {
  if (file_) {
    fflush(file_);
    fclose(file_);
    file_ = nullptr;
  }
}

}  //  namespace gpu
}  //  namespace xe
