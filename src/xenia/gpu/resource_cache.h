/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_RESOURCE_CACHE_H_
#define XENIA_GPU_RESOURCE_CACHE_H_

#include <map>
#include <unordered_map>

#include <xenia/core.h>
#include <xenia/gpu/buffer_resource.h>
#include <xenia/gpu/resource.h>
#include <xenia/gpu/sampler_state_resource.h>
#include <xenia/gpu/shader_resource.h>
#include <xenia/gpu/texture_resource.h>
#include <xenia/gpu/xenos/xenos.h>


namespace xe {
namespace gpu {


class ResourceCache {
public:
  virtual ~ResourceCache();

  VertexShaderResource* FetchVertexShader(
      const MemoryRange& memory_range,
      const VertexShaderResource::Info& info);
  PixelShaderResource* FetchPixelShader(
      const MemoryRange& memory_range,
      const PixelShaderResource::Info& info);
  
  TextureResource* FetchTexture(
      const MemoryRange& memory_range,
      const TextureResource::Info& info);
  SamplerStateResource* FetchSamplerState(
      const SamplerStateResource::Info& info);

  IndexBufferResource* FetchIndexBuffer(
      const MemoryRange& memory_range,
      const IndexBufferResource::Info& info);
  VertexBufferResource* FetchVertexBuffer(
      const MemoryRange& memory_range,
      const VertexBufferResource::Info& info);

  uint64_t HashRange(const MemoryRange& memory_range);

  void SyncRange(uint32_t address, int length);

protected:
  ResourceCache(Memory* memory);

  template <typename T, typename V>
  T* FetchHashedResource(const MemoryRange& memory_range,
                         const typename T::Info& info,
                         const V& factory) {
    // TODO(benvanik): if there's no way it's changed and it's been checked,
    //     just lookup. This way we don't rehash 100x a frame.
    auto key = HashRange(memory_range);
    auto it = hashed_resources_.find(key);
    if (it != hashed_resources_.end()) {
      return static_cast<T*>(it->second);
    }
    auto resource = (this->*factory)(memory_range, info);
    hashed_resources_.insert({ key, resource });
    resources_.push_back(resource);
    return resource;
  }

  template <typename T, typename V>
  T* FetchPagedResource(const MemoryRange& memory_range,
                        const typename T::Info& info,
                        const V& factory) {
    uint32_t lo_address = memory_range.guest_base % 0x20000000;
    auto key = uint64_t(lo_address);
    auto range = paged_resources_.equal_range(key);
    for (auto it = range.first; it != range.second; ++it) {
      if (it->second->memory_range().length == memory_range.length &&
          it->second->Equals(info)) {
        return static_cast<T*>(it->second);
      }
    }
    auto resource = (this->*factory)(memory_range, info);
    paged_resources_.insert({ key, resource });
    resources_.push_back(resource);
    return resource;
  }
  
  virtual VertexShaderResource* CreateVertexShader(
      const MemoryRange& memory_range,
      const VertexShaderResource::Info& info) = 0;
  virtual PixelShaderResource* CreatePixelShader(
      const MemoryRange& memory_range,
      const PixelShaderResource::Info& info) = 0;
  virtual TextureResource* CreateTexture(
      const MemoryRange& memory_range,
      const TextureResource::Info& info) = 0;
  virtual SamplerStateResource* CreateSamplerState(
      const SamplerStateResource::Info& info) = 0;
  virtual IndexBufferResource* CreateIndexBuffer(
      const MemoryRange& memory_range,
      const IndexBufferResource::Info& info) = 0;
  virtual VertexBufferResource* CreateVertexBuffer(
      const MemoryRange& memory_range,
      const VertexBufferResource::Info& info) = 0;

private:
  Memory* memory_;

  std::vector<Resource*> resources_;
  std::unordered_map<uint64_t, HashedResource*> hashed_resources_;
  std::unordered_map<uint64_t, StaticResource*> static_resources_;
  std::multimap<uint64_t, PagedResource*> paged_resources_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_RESOURCE_CACHE_H_
