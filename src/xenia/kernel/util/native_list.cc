/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/native_list.h"

namespace xe {
namespace kernel {
namespace util {

NativeList::NativeList(Memory* memory)
    : memory_(memory), head_(kInvalidPointer) {}

void NativeList::Insert(uint32_t ptr) {
  xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(ptr + 0), head_);
  xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(ptr + 4), 0);
  if (head_) {
    xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(head_ + 4), ptr);
  }
  head_ = ptr;
}

bool NativeList::IsQueued(uint32_t ptr) {
  uint32_t flink =
      xe::load_and_swap<uint32_t>(memory_->TranslateVirtual(ptr + 0));
  uint32_t blink =
      xe::load_and_swap<uint32_t>(memory_->TranslateVirtual(ptr + 4));
  return head_ == ptr || flink || blink;
}

void NativeList::Remove(uint32_t ptr) {
  uint32_t flink =
      xe::load_and_swap<uint32_t>(memory_->TranslateVirtual(ptr + 0));
  uint32_t blink =
      xe::load_and_swap<uint32_t>(memory_->TranslateVirtual(ptr + 4));
  if (ptr == head_) {
    head_ = flink;
    if (flink) {
      xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(flink + 4), 0);
    }
  } else {
    if (blink) {
      xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(blink + 0), flink);
    }
    if (flink) {
      xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(flink + 4), blink);
    }
  }
  xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(ptr + 0), 0);
  xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(ptr + 4), 0);
}

uint32_t NativeList::Shift() {
  if (!head_) {
    return 0;
  }

  uint32_t ptr = head_;
  Remove(ptr);
  return ptr;
}

bool NativeList::HasPending() { return head_ != kInvalidPointer; }

}  // namespace util
}  // namespace kernel
}  // namespace xe
