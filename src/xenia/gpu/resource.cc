/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/resource.h>


using namespace std;
using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


HashedResource::HashedResource(const MemoryRange& memory_range)
    : memory_range_(memory_range) {
}

HashedResource::~HashedResource() = default;

PagedResource::PagedResource(const MemoryRange& memory_range)
    : memory_range_(memory_range), dirtied_(true) {
}

PagedResource::~PagedResource() = default;

void PagedResource::MarkDirty(uint32_t lo_address, uint32_t hi_address) {
  dirtied_ = true;
}

StaticResource::StaticResource() = default;

StaticResource::~StaticResource() = default;
