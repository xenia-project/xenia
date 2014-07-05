/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_RESOURCE_H_
#define XENIA_GPU_RESOURCE_H_

#include <xenia/core.h>
#include <xenia/gpu/xenos/xenos.h>


namespace xe {
namespace gpu {


struct MemoryRange {
  uint8_t* host_base;
  uint32_t guest_base;
  uint32_t length;

  MemoryRange() : host_base(nullptr), guest_base(0), length(0) {}
  MemoryRange(const MemoryRange& other)
      : host_base(other.host_base), guest_base(other.guest_base),
        length(other.length) {}
  MemoryRange(uint8_t* _host_base, uint32_t _guest_base, uint32_t _length)
      : host_base(_host_base), guest_base(_guest_base), length(_length) {}
};


class Resource {
public:
  virtual ~Resource() = default;

  virtual void* handle() const = 0;

  template <typename T>
  T* handle_as() {
    return reinterpret_cast<T*>(handle());
  }

protected:
  Resource() = default;

  // last use/LRU stuff
};


class HashedResource : public Resource {
public:
  ~HashedResource() override;

  const MemoryRange& memory_range() const { return memory_range_; }

protected:
  HashedResource(const MemoryRange& memory_range);

  MemoryRange memory_range_;
  // key
};


class PagedResource : public Resource {
public:
  ~PagedResource() override;

  const MemoryRange& memory_range() const { return memory_range_; }

  template <typename T>
  bool Equals(const T& info) {
    return Equals(&info, sizeof(info));
  }
  virtual bool Equals(const void* info_ptr, size_t info_length) = 0;

  bool is_dirty() const { return dirtied_; }
  void MarkDirty(uint32_t lo_address, uint32_t hi_address);

protected:
  PagedResource(const MemoryRange& memory_range);

  MemoryRange memory_range_;
  bool dirtied_;
  // dirtied pages list
};


class StaticResource : public Resource {
public:
  ~StaticResource() override;

protected:
  StaticResource();
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_RESOURCE_H_
