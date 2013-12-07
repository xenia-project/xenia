/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_TRACING_CHANNEL_H_
#define ALLOY_TRACING_CHANNEL_H_

#include <alloy/core.h>


namespace alloy {
namespace tracing {


class Channel {
public:
  Channel();
  virtual ~Channel();

  virtual void Write(
      size_t buffer_count,
      size_t buffer_lengths[], const uint8_t* buffers[]) = 0;
  virtual void Flush() = 0;
};


}  // namespace tracing
}  // namespace alloy


#endif  // ALLOY_TRACING_CHANNEL_H_
