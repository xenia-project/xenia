/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/tracing/tracing.h>

#include <thread>
#include <gflags/gflags.h>

#include <alloy/tracing/channel.h>
#include <alloy/tracing/event_type.h>
#include <alloy/tracing/tracer.h>
#include <alloy/tracing/channels/file_channel.h>

DEFINE_string(trace_file, "", "Traces to the given file path.");
// trace shared memory
// trace socket

namespace alloy {
namespace tracing {

Channel* shared_channel = nullptr;
thread_local Tracer* thread_tracer = nullptr;

void CleanupTracing() {
  if (shared_channel) {
    alloy::tracing::WriteEvent(EventType::TraceEOF({}));
    shared_channel->Flush();
    shared_channel = nullptr;
  }
}

bool Initialize(Channel* channel) {
  if (shared_channel) {
    return false;
  }
  if (!channel) {
    // Create from flags.
    if (FLAGS_trace_file.size()) {
      channel = new channels::FileChannel(FLAGS_trace_file.c_str());
    }
    if (!channel) {
      // Tracing disabled.
      return true;
    }
  }
  shared_channel = channel;
  alloy::tracing::WriteEvent(EventType::TraceInit({}));
  channel->Flush();
  atexit(CleanupTracing);
  return true;
}

void Shutdown() {
  // ?
}

void Flush() {
  if (shared_channel) {
    shared_channel->Flush();
  }
}

Tracer* GetThreadTracer() {
  if (!shared_channel) {
    return nullptr;
  }
  if (!thread_tracer) {
    thread_tracer = new Tracer(shared_channel);
  }
  return thread_tracer;
}

void WriteEvent(uint32_t event_type, size_t size, const void* data) {
  Tracer* t = GetThreadTracer();
  if (t) {
    t->WriteEvent(event_type, size, (const uint8_t*)data);
  }
}

}  // namespace tracing
}  // namespace alloy
