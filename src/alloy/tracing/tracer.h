/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_TRACING_TRACER_H_
#define ALLOY_TRACING_TRACER_H_

#include <alloy/core.h>


namespace alloy {
namespace tracing {

class Channel;


class Tracer {
public:
  Tracer(Channel* channel);
  ~Tracer();

  int thread_id() const { return thread_id_; }
  void set_thread_id(int value) { thread_id_ = value; }

  void WriteEvent(
      uint32_t event_type, size_t size = 0, const uint8_t* data = 0);

private:
  Channel* channel_;
  int thread_id_;
};


}  // namespace tracing
}  // namespace alloy


#endif  // ALLOY_TRACING_TRACER_H_
