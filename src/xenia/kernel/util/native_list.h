/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_NATIVE_LIST_H_
#define XENIA_KERNEL_UTIL_NATIVE_LIST_H_

#include "xenia/memory.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace util {

// List is designed for storing pointers to objects in the guest heap.
// All values in the list should be assumed to be in big endian.

// Pass LIST_ENTRY pointers.
// struct MYOBJ {
//   uint32_t stuff;
//   LIST_ENTRY list_entry; <-- pass this
//   ...
// }

class NativeList {
 public:
  explicit NativeList(Memory* memory);

  void Insert(uint32_t list_entry_ptr);
  bool IsQueued(uint32_t list_entry_ptr);
  void Remove(uint32_t list_entry_ptr);
  uint32_t Shift();
  bool HasPending();

 private:
  const uint32_t kInvalidPointer = 0xE0FE0FFF;

 private:
  Memory* memory_;
  uint32_t head_;
};

}  // namespace util
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_NATIVE_LIST_H_
