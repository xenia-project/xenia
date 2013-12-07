/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_TRACING_CHANNELS_FILE_CHANNEL_H_
#define ALLOY_TRACING_CHANNELS_FILE_CHANNEL_H_

#include <alloy/core.h>

#include <alloy/tracing/channel.h>


namespace alloy {
namespace tracing {
namespace channels {


class FileChannel : public Channel {
public:
  FileChannel(const char* path);
  virtual ~FileChannel();

  virtual void Write(
      size_t buffer_count,
      size_t buffer_lengths[], const uint8_t* buffers[]);

  virtual void Flush();

private:
  char* path_;
  FILE* file_;
  Mutex* lock_;
};


}  // namespace channels
}  // namespace tracing
}  // namespace alloy


#endif  // ALLOY_TRACING_CHANNELS_FILE_CHANNEL_H_
