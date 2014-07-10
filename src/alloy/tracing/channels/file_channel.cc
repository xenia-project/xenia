/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/tracing/channels/file_channel.h>

using namespace alloy;
using namespace alloy::tracing;
using namespace alloy::tracing::channels;


FileChannel::FileChannel(const char* path) {
  path_ = xestrdupa(path);
  file_ = fopen(path, "wb");
}

FileChannel::~FileChannel() {
  std::lock_guard<std::mutex> guard(lock_);
  fclose(file_);
  file_ = 0;
  free(path_);
  path_ = 0;
}

void FileChannel::Write(
    size_t buffer_count,
    size_t buffer_lengths[], const uint8_t* buffers[]) {
  std::lock_guard<std::mutex> guard(lock_);
  if (file_) {
    for (size_t n = 0; n < buffer_count; n++) {
      fwrite(buffers[n], buffer_lengths[n], 1, file_);
    }
  }
}

void FileChannel::Flush() {
  std::lock_guard<std::mutex> guard(lock_);
  if (file_) {
    fflush(file_);
  }
}
