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

  // TODO(benvanik): remove with GPU refactor.
  virtual uint64_t page_table() const = 0;

  uint64_t trace_base() const { return trace_base_; }
  void set_trace_base(uint64_t value) { trace_base_ = value; }

  virtual int Initialize();

  // TODO(benvanik): make poly memory utils for these.
  void Zero(uint64_t address, size_t size);
  void Fill(uint64_t address, size_t size, uint8_t value);
  void Copy(uint64_t dest, uint64_t src, size_t size);

  uint64_t SearchAligned(uint64_t start, uint64_t end, const uint32_t* values,
                         size_t value_count);

  // TODO(benvanik): remove with IVM.
  virtual uint8_t LoadI8(uint64_t address) = 0;
  virtual uint16_t LoadI16(uint64_t address) = 0;
  virtual uint32_t LoadI32(uint64_t address) = 0;
  virtual uint64_t LoadI64(uint64_t address) = 0;
  virtual void StoreI8(uint64_t address, uint8_t value) = 0;
  virtual void StoreI16(uint64_t address, uint16_t value) = 0;
  virtual void StoreI32(uint64_t address, uint32_t value) = 0;
  virtual void StoreI64(uint64_t address, uint64_t value) = 0;

 protected:
  size_t system_page_size_;
  uint8_t* membase_;
  uint64_t reserve_address_;
  uint64_t trace_base_;
};

class SimpleMemory : public Memory {
 public:
  SimpleMemory(size_t capacity);
  ~SimpleMemory() override;

  uint64_t page_table() const override { return 0; }

  // TODO(benvanik): remove with IVM.
  uint8_t LoadI8(uint64_t address) override;
  uint16_t LoadI16(uint64_t address) override;
  uint32_t LoadI32(uint64_t address) override;
  uint64_t LoadI64(uint64_t address) override;
  void StoreI8(uint64_t address, uint8_t value) override;
  void StoreI16(uint64_t address, uint16_t value) override;
  void StoreI32(uint64_t address, uint32_t value) override;
  void StoreI64(uint64_t address, uint64_t value) override;

 private:
  std::vector<uint8_t> memory_;
};

}  // namespace alloy

#endif  // ALLOY_MEMORY_H_
