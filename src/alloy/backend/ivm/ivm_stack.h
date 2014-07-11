/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_IVM_IVM_STACK_H_
#define ALLOY_BACKEND_IVM_IVM_STACK_H_

#include <alloy/core.h>

#include <alloy/backend/ivm/ivm_intcode.h>

namespace alloy {
namespace backend {
namespace ivm {

class IVMStack {
 public:
  IVMStack();
  ~IVMStack();

  Register* Alloc(size_t register_count);
  void Free(size_t register_count);

 private:
  class Chunk {
   public:
    Chunk(size_t chunk_size);
    ~Chunk();

    Chunk* prev;
    Chunk* next;

    size_t capacity;
    uint8_t* buffer;
    size_t offset;
  };

 private:
  size_t chunk_size_;
  Chunk* head_chunk_;
  Chunk* active_chunk_;
};

}  // namespace ivm
}  // namespace backend
}  // namespace alloy

#endif  // ALLOY_BACKEND_IVM_IVM_STACK_H_
