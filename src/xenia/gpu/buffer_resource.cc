/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/buffer_resource.h>


using namespace std;
using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


BufferResource::BufferResource(const MemoryRange& memory_range)
    : PagedResource(memory_range) {
}

BufferResource::~BufferResource() = default;

int BufferResource::Prepare() {
  if (!handle()) {
    if (CreateHandle()) {
      XELOGE("Unable to create buffer handle");
      return 1;
    }
  }

  if (!dirtied_) {
    return 0;
  }
  dirtied_ = false;

  // pass dirty regions?
  return InvalidateRegion(memory_range_);
}

IndexBufferResource::IndexBufferResource(const MemoryRange& memory_range,
                                         const Info& info)
    : BufferResource(memory_range),
      info_(info) {
}

IndexBufferResource::~IndexBufferResource() = default;

VertexBufferResource::VertexBufferResource(const MemoryRange& memory_range,
                                          const Info& info)
    : BufferResource(memory_range),
      info_(info) {
}

VertexBufferResource::~VertexBufferResource() = default;
