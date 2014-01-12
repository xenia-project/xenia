/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/native_list.h>


using namespace xe;
using namespace xe::kernel;


NativeList::NativeList(Memory* memory) :
    memory_(memory),
    head_(kInvalidPointer) {
}

void NativeList::Insert(uint32_t ptr) {
  uint8_t* mem = memory_->membase();
  XESETUINT32BE(mem + ptr + 0, head_);
  XESETUINT32BE(mem + ptr + 4, 0);
  if (head_) {
    XESETUINT32BE(mem + head_ + 4, ptr);
  }
  head_ = ptr;
}

bool NativeList::IsQueued(uint32_t ptr) {
  uint8_t* mem = memory_->membase();
  uint32_t flink = XEGETUINT32BE(mem + ptr + 0);
  uint32_t blink = XEGETUINT32BE(mem + ptr + 4);
  return head_ == ptr || flink || blink;
}

void NativeList::Remove(uint32_t ptr) {
  uint8_t* mem = memory_->membase();
  uint32_t flink = XEGETUINT32BE(mem + ptr + 0);
  uint32_t blink = XEGETUINT32BE(mem + ptr + 4);
  if (ptr == head_) {
    head_ = flink;
    if (flink) {
      XESETUINT32BE(mem + flink + 4, 0);
    }
  } else {
    if (blink) {
      XESETUINT32BE(mem + blink + 0, flink);
    }
    if (flink) {
      XESETUINT32BE(mem + flink + 4, blink);
    }
  }
  XESETUINT32BE(mem + ptr + 0, 0);
  XESETUINT32BE(mem + ptr + 4, 0);
}

uint32_t NativeList::Shift() {
  if (!head_) {
    return 0;
  }

  uint32_t ptr = head_;
  Remove(ptr);
  return ptr;
}
