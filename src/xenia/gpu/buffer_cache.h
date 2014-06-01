/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_BUFFER_CACHE_H_
#define XENIA_GPU_BUFFER_CACHE_H_

#include <xenia/core.h>
#include <xenia/gpu/buffer.h>
#include <xenia/gpu/xenos/xenos.h>


namespace xe {
namespace gpu {


class BufferCache {
public:
  BufferCache();
  virtual ~BufferCache();

  IndexBuffer* FetchIndexBuffer(
      const IndexBufferInfo& info,
      const uint8_t* src_ptr, size_t length);

  void Clear();

protected:
  virtual IndexBuffer* CreateIndexBuffer(
      const IndexBufferInfo& info,
      const uint8_t* src_ptr, size_t length) = 0;

private:
  std::unordered_map<uint64_t, IndexBuffer*> index_buffer_map_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_BUFFER_CACHE_H_
