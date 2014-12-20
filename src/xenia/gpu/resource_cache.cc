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

#include <xenia/core/hash.h>

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
  return hash64(memory_range.host_base, memory_range.length);
}

void ResourceCache::SyncRange(uint32_t address, int length) {
  SCOPE_profile_cpu_f("gpu");
  // TODO(benvanik): something interesting?
  //uint32_t lo_address = address % 0x20000000;
  //uint32_t hi_address = lo_address + length;
}
