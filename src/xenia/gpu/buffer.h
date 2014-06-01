/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_BUFFER_H_
#define XENIA_GPU_BUFFER_H_

#include <xenia/core.h>
#include <xenia/gpu/xenos/ucode.h>
#include <xenia/gpu/xenos/xenos.h>


namespace xe {
namespace gpu {


class Buffer {
public:
  Buffer(const uint8_t* src_ptr, size_t length);
  virtual ~Buffer();

  const uint8_t* src() const { return src_; }
  size_t length() const { return length_; }
  uint64_t hash() const { return hash_; }

  virtual bool FetchNew(uint64_t hash) = 0;
  virtual bool FetchDirty(uint64_t hash) = 0;

protected:
  const uint8_t* src_;
  size_t      length_;
  uint64_t    hash_;
};


struct IndexBufferInfo {
  bool index_32bit;
  uint32_t index_count;
  uint32_t index_size;
  uint32_t endianness;
};


class IndexBuffer : public Buffer {
public:
  IndexBuffer(const IndexBufferInfo& info,
              const uint8_t* src_ptr, size_t length);
  virtual ~IndexBuffer();

protected:
  IndexBufferInfo info_;
};


class VertexBuffer : public Buffer {
public:
  VertexBuffer(const uint8_t* src_ptr, size_t length);
  virtual ~VertexBuffer();
};



}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_BUFFER_H_
