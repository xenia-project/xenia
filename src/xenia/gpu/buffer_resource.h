/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_BUFFER_RESOURCE_H_
#define XENIA_GPU_BUFFER_RESOURCE_H_

#include <xenia/gpu/resource.h>
#include <xenia/gpu/xenos/ucode.h>
#include <xenia/gpu/xenos/xenos.h>


namespace xe {
namespace gpu {


class BufferResource : public PagedResource {
public:
  BufferResource(const MemoryRange& memory_range);
  ~BufferResource() override;

  virtual int Prepare();

protected:
  virtual int CreateHandle() = 0;
  virtual int InvalidateRegion(const MemoryRange& memory_range) = 0;
};


enum IndexFormat {
  INDEX_FORMAT_16BIT = 0,
  INDEX_FORMAT_32BIT = 1,
};

class IndexBufferResource : public BufferResource {
public:
  struct Info {
    IndexFormat format;
    xenos::XE_GPU_ENDIAN endianness;
  };

  IndexBufferResource(const MemoryRange& memory_range,
                      const Info& info);
  ~IndexBufferResource() override;

  const Info& info() const { return info_; }

  bool Equals(const void* info_ptr, size_t info_length) override {
    return info_length == sizeof(Info) &&
           memcmp(info_ptr, &info_, info_length) == 0;
  }

protected:
  Info info_;
};


class VertexBufferResource : public BufferResource {
public:
  struct DeclElement {
    xenos::instr_fetch_vtx_t vtx_fetch;
    uint32_t format;
    uint32_t offset_words;
    uint32_t size_words;
    bool is_signed;
    bool is_normalized;
  };
  struct Info {
    uint32_t stride_words;
    uint32_t element_count;
    DeclElement elements[16];
  };

  VertexBufferResource(const MemoryRange& memory_range,
                       const Info& info);
  ~VertexBufferResource() override;

  const Info& info() const { return info_; }

  bool Equals(const void* info_ptr, size_t info_length) override {
    return info_length == sizeof(Info) &&
           memcmp(info_ptr, &info_, info_length) == 0;
  }

protected:
  Info info_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_BUFFER_RESOURCE_H_
