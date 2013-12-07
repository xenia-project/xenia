/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_TRACING_TRACING_H_
#define ALLOY_TRACING_TRACING_H_

#include <alloy/core.h>

#include <alloy/tracing/event_type.h>


namespace alloy {
namespace tracing {

class Channel;
class Tracer;


bool Initialize(Channel* channel = 0);
void Shutdown();
void Flush();

Tracer* GetThreadTracer();

void WriteEvent(uint32_t event_type, size_t size = 0, const void* data = 0);

template<typename T> void WriteEvent(T& ev) {
  if (sizeof(T) > 1) {
    alloy::tracing::WriteEvent(T::event_type, sizeof(T), &ev);
  } else {
    alloy::tracing::WriteEvent(T::event_type);
  }
}


}  // namespace tracing
}  // namespace alloy


#endif  // ALLOY_TRACING_TRACING_H_
