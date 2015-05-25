/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XENUMERATOR_H_
#define XENIA_KERNEL_XBOXKRNL_XENUMERATOR_H_

#include <vector>

#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class XEnumerator : public XObject {
 public:
  XEnumerator(KernelState* kernel_state, size_t item_capacity,
              size_t item_size);
  virtual ~XEnumerator();

  void Initialize();

  virtual uint32_t item_count() const = 0;
  virtual void WriteItems(uint8_t* buffer) = 0;

 protected:
  size_t item_capacity_;
  size_t item_size_;
};

class XStaticEnumerator : public XEnumerator {
 public:
  XStaticEnumerator(KernelState* kernel_state, size_t item_capacity,
                    size_t item_size)
      : XEnumerator(kernel_state, item_capacity, item_size), item_count_(0) {
    buffer_.resize(item_capacity_ * item_size_);
  }

  uint32_t item_count() const override { return item_count_; }

  uint8_t* AppendItem() {
    if (item_count_ + 1 > item_capacity_) {
      return nullptr;
    }
    auto ptr = const_cast<uint8_t*>(buffer_.data() + item_count_ * item_size_);
    ++item_count_;
    return ptr;
  }

  void WriteItems(uint8_t* buffer) override {
    std::memcpy(buffer, buffer_.data(), item_count_ * item_size_);
  }

 private:
  uint32_t item_count_;
  std::vector<uint8_t> buffer_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XENUMERATOR_H_
