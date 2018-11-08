/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XENUMERATOR_H_
#define XENIA_KERNEL_XENUMERATOR_H_

#include <cstring>
#include <vector>

#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class XEnumerator : public XObject {
 public:
  static const Type kType = kTypeEnumerator;

  XEnumerator(KernelState* kernel_state, size_t items_per_enumerate,
              size_t item_size);
  virtual ~XEnumerator();

  void Initialize();

  virtual uint32_t item_count() const = 0;
  virtual void WriteItems(uint8_t* buffer) = 0;
  virtual bool WriteItem(uint8_t* buffer) = 0;

  size_t item_size() const { return item_size_; }
  size_t items_per_enumerate() const { return items_per_enumerate_; }
  size_t current_item() const { return current_item_; }

 protected:
  size_t items_per_enumerate_ = 0;
  size_t item_size_ = 0;
  size_t current_item_ = 0;
};

class XStaticEnumerator : public XEnumerator {
 public:
  XStaticEnumerator(KernelState* kernel_state, size_t items_per_enumerate,
                    size_t item_size)
      : XEnumerator(kernel_state, items_per_enumerate, item_size),
        item_count_(0) {}

  uint32_t item_count() const override { return item_count_; }

  uint8_t* AppendItem() {
    buffer_.resize(++item_count_ * item_size_);
    auto ptr =
        const_cast<uint8_t*>(buffer_.data() + (item_count_ - 1) * item_size_);
    return ptr;
  }

  void WriteItems(uint8_t* buffer) override {
    std::memcpy(buffer, buffer_.data(), item_count_ * item_size_);
  }

  bool WriteItem(uint8_t* buffer) override {
    if (current_item_ >= item_count_) {
      return false;
    }

    std::memcpy(buffer, buffer_.data() + current_item_ * item_size_,
                item_size_);
    current_item_++;

    return true;
  }

 private:
  uint32_t item_count_;
  std::vector<uint8_t> buffer_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XENUMERATOR_H_
