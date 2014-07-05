/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/resource_cache.h>

#include <algorithm>


using namespace std;
using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


ResourceCache::ResourceCache(Memory* memory)
    : memory_(memory) {
}

ResourceCache::~ResourceCache() {
  for (auto it = resources_.begin(); it != resources_.end(); ++it) {
    Resource* resource = *it;
    delete resource;
  }
  resources_.clear();
}

VertexShaderResource* ResourceCache::FetchVertexShader(
    const MemoryRange& memory_range,
    const VertexShaderResource::Info& info) {
  return FetchHashedResource<VertexShaderResource>(
      memory_range, info, &ResourceCache::CreateVertexShader);
}

PixelShaderResource* ResourceCache::FetchPixelShader(
    const MemoryRange& memory_range,
    const PixelShaderResource::Info& info) {
  return FetchHashedResource<PixelShaderResource>(
      memory_range, info, &ResourceCache::CreatePixelShader);
}

TextureResource* ResourceCache::FetchTexture(
    const MemoryRange& memory_range,
    const TextureResource::Info& info) {
  auto resource = FetchPagedResource<TextureResource>(
      memory_range, info, &ResourceCache::CreateTexture);
  if (!resource) {
    return nullptr;
  }
  if (resource->Prepare()) {
    XELOGE("Unable to prepare texture");
    return nullptr;
  }
  return resource;
}

SamplerStateResource* ResourceCache::FetchSamplerState(
    const SamplerStateResource::Info& info) {
  auto key = info.hash();
  auto it = static_resources_.find(key);
  if (it != static_resources_.end()) {
    return static_cast<SamplerStateResource*>(it->second);
  }
  auto resource = CreateSamplerState(info);
  if (resource->Prepare()) {
    XELOGE("Unable to prepare sampler state");
    return nullptr;
  }
  static_resources_.insert({ key, resource });
  resources_.push_back(resource);
  return resource;
}

IndexBufferResource* ResourceCache::FetchIndexBuffer(
    const MemoryRange& memory_range,
    const IndexBufferResource::Info& info) {
  auto resource = FetchPagedResource<IndexBufferResource>(
      memory_range, info, &ResourceCache::CreateIndexBuffer);
  if (!resource) {
    return nullptr;
  }
  if (resource->Prepare()) {
    XELOGE("Unable to prepare index buffer");
    return nullptr;
  }
  return resource;
}

VertexBufferResource* ResourceCache::FetchVertexBuffer(
    const MemoryRange& memory_range,
    const VertexBufferResource::Info& info) {
  auto resource = FetchPagedResource<VertexBufferResource>(
      memory_range, info, &ResourceCache::CreateVertexBuffer);
  if (!resource) {
    return nullptr;
  }
  if (resource->Prepare()) {
    XELOGE("Unable to prepare vertex buffer");
    return nullptr;
  }
  return resource;
}

uint64_t ResourceCache::HashRange(const MemoryRange& memory_range) {
  // We could do something smarter here to potentially early exit.
  return xe_hash64(memory_range.host_base, memory_range.length);
}

void ResourceCache::SyncRange(uint32_t address, int length) {
  SCOPE_profile_cpu_f("gpu");

  // Scan the page table in sync with our resource list. This means
  // we have O(n) complexity for updates, though we could definitely
  // make this faster/cleaner.
  // TODO(benvanik): actually do this right.
  // For now we assume the page table in the range of our resources
  // will not be changing, which allows us to do a foreach(res) and reload
  // and then clear the table.

  // total bytes = (512 * 1024 * 1024) / (16 * 1024) = 32768
  // each byte = 1 page
  // Walk as qwords so we can clear things up faster.
  uint64_t* page_table = reinterpret_cast<uint64_t*>(
      memory_->Translate(memory_->page_table()));
  uint32_t page_size = 16 * 1024;  // 16KB pages

  uint32_t lo_address = address % 0x20000000;
  uint32_t hi_address = lo_address + length;
  hi_address = (hi_address / page_size) * page_size + page_size;
  int start_page = lo_address / page_size;
  int end_page = hi_address / page_size;

  {
    SCOPE_profile_cpu_i("gpu", "SyncRange:mark");
    auto it = lo_address > page_size ?
        paged_resources_.upper_bound(lo_address - page_size) :
        paged_resources_.begin();
    auto end_it = paged_resources_.lower_bound(hi_address + page_size);
    while (it != end_it) {
      const auto& memory_range = it->second->memory_range();
      int lo_page = (memory_range.guest_base % 0x20000000) / page_size;
      int hi_page = lo_page + (memory_range.length / page_size);
      lo_page = std::max(lo_page, start_page);
      hi_page = std::min(hi_page, end_page);
      if (lo_page > hi_page) {
        ++it;
        continue;
      }
      for (int i = lo_page / 8; i <= hi_page / 8; ++i) {
        uint64_t page_flags = page_table[i];
        if (page_flags) {
          // Dirty!
          it->second->MarkDirty(i * 8 * page_size, (i * 8 + 7) * page_size);
        }
      }
      ++it;
    }
  }

  // Reset page table.
  {
    SCOPE_profile_cpu_i("gpu", "SyncRange:reset");
    for (auto i = start_page / 8; i <= end_page / 8; ++i) {
      page_table[i] = 0;
    }
  }
}
