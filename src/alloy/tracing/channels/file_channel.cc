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
  lock_ = AllocMutex(10000);
  path_ = xestrdupa(path);
  file_ = fopen(path, "wb");
}

FileChannel::~FileChannel() {
  LockMutex(lock_);
  fclose(file_);
  file_ = 0;
  free(path_);
  path_ = 0;
  UnlockMutex(lock_);
  FreeMutex(lock_);
}

void FileChannel::Write(
    size_t buffer_count,
    size_t buffer_lengths[], const uint8_t* buffers[]) {
  LockMutex(lock_);
  if (file_) {
    for (size_t n = 0; n < buffer_count; n++) {
      fwrite(buffers[n], buffer_lengths[n], 1, file_);
    }
  }
  UnlockMutex(lock_);
}

void FileChannel::Flush() {
  LockMutex(lock_);
  if (file_) {
    fflush(file_);
  }
  UnlockMutex(lock_);
}
