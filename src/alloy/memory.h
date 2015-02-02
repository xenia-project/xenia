/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_MEMORY_H_
#define ALLOY_MEMORY_H_

#include <cstdint>
#include <vector>

namespace alloy {

class Memory {
 public:
  Memory();
  virtual ~Memory();

  inline uint8_t* membase() const { return membase_; }
  inline uint8_t* Translate(uint64_t guest_address) const {
    return membase_ + guest_address;
  };
  inline uint64_t* reserve_address() { return &reserve_address_; }
  inline uint64_t* reserve_value() { return &reserve_value_; }

  uint64_t trace_base() const { return trace_base_; }
  void set_trace_base(uint64_t value) { trace_base_ = value; }

  virtual int Initialize();

  // TODO(benvanik): make poly memory utils for these.
  void Zero(uint64_t address, size_t size);
  void Fill(uint64_t address, size_t size, uint8_t value);
  void Copy(uint64_t dest, uint64_t src, size_t size);

  uint64_t SearchAligned(uint64_t start, uint64_t end, const uint32_t* values,
                         size_t value_count);

 protected:
  size_t system_page_size_;
  uint8_t* membase_;
  uint64_t reserve_address_;
  uint64_t reserve_value_;
  uint64_t trace_base_;
};

class SimpleMemory : public Memory {
 public:
  SimpleMemory(size_t capacity);
  ~SimpleMemory() override;

 private:
  std::vector<uint8_t> memory_;
};

}  // namespace alloy

#endif  // ALLOY_MEMORY_H_
