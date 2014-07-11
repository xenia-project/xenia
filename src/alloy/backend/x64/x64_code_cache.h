/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_X64_CODE_CACHE_H_
#define ALLOY_BACKEND_X64_X64_CODE_CACHE_H_

#include <mutex>

#include <alloy/core.h>

namespace alloy {
namespace backend {
namespace x64 {

class X64CodeChunk;

class X64CodeCache {
 public:
  X64CodeCache(size_t chunk_size = DEFAULT_CHUNK_SIZE);
  virtual ~X64CodeCache();

  int Initialize();

  // TODO(benvanik): ELF serialization/etc
  // TODO(benvanik): keep track of code blocks
  // TODO(benvanik): padding/guards/etc

  void* PlaceCode(void* machine_code, size_t code_size, size_t stack_size);

 private:
  const static size_t DEFAULT_CHUNK_SIZE = 4 * 1024 * 1024;
  std::mutex lock_;
  size_t chunk_size_;
  X64CodeChunk* head_chunk_;
  X64CodeChunk* active_chunk_;
};

}  // namespace x64
}  // namespace backend
}  // namespace alloy

#endif  // ALLOY_BACKEND_X64_X64_CODE_CACHE_H_
