/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/tracing/tracer.h>

#include <alloy/tracing/channel.h>

using namespace alloy;
using namespace alloy::tracing;


namespace {

volatile int next_thread_id_ = 0x10000000;

};


Tracer::Tracer(Channel* channel) :
    channel_(channel) {
  thread_id_ = xe_atomic_inc_32(&next_thread_id_);
}

Tracer::~Tracer() {
}

void Tracer::WriteEvent(
    uint32_t event_type, size_t size, const uint8_t* data) {
  uint32_t header[] = {
    event_type,
    (uint32_t)thread_id_,
    0, // time in us
    (uint32_t)size,
  };
  size_t buffer_count = size ? 2 : 1;
  size_t buffer_lengths[] = {
    sizeof(header),
    size,
  };
  const uint8_t* buffers[] = {
    (const uint8_t*)header,
    data,
  };
  channel_->Write(buffer_count, buffer_lengths, buffers);
}
