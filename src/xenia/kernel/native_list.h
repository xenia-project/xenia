/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_NATIVE_LIST_H_
#define XENIA_KERNEL_XBOXKRNL_NATIVE_LIST_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/xbox.h>


namespace xe {
namespace kernel {


// List is designed for storing pointers to objects in the guest heap.
// All values in the list should be assumed to be in big endian.

// Entries, as kernel objects, are assumed to have a LIST_ENTRY struct at +4b.
// struct MYOBJ {
//   uint32_t stuff;
//   LIST_ENTRY list_entry; <-- manipulated
//   ...
// }

class NativeList {
public:
  NativeList(Memory* memory);

  void Insert(uint32_t ptr);
  bool IsQueued(uint32_t ptr);
  void Remove(uint32_t ptr);
  uint32_t Shift();

private:
  const uint32_t kInvalidPointer = 0xE0FE0FFF;

private:
  Memory* memory_;
  uint32_t head_;
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_NATIVE_LIST_H_
