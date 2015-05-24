/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_MEMORY_H_
#define XENIA_MEMORY_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "xenia/base/platform.h"
#include "xenia/cpu/mmio_handler.h"

namespace xe {

enum SystemHeapFlag : uint32_t {
  kSystemHeapVirtual = 1 << 0,
  kSystemHeapPhysical = 1 << 1,

  kSystemHeapDefault = kSystemHeapVirtual,
};

enum MemoryAllocationFlag : uint32_t {
  kMemoryAllocationReserve = 1 << 0,
  kMemoryAllocationCommit = 1 << 1,
};

enum MemoryProtectFlag : uint32_t {
  kMemoryProtectRead = 1 << 0,
  kMemoryProtectWrite = 1 << 1,
  kMemoryProtectNoCache = 1 << 2,
  kMemoryProtectWriteCombine = 1 << 3,

  kMemoryProtectNoAccess = 0,
};

// Equivalent to the Win32 MEMORY_BASIC_INFORMATION struct.
struct HeapAllocationInfo {
  // A pointer to the base address of the region of pages.
  uint32_t base_address;
  // A pointer to the base address of a range of pages allocated by the
  // VirtualAlloc function. The page pointed to by the BaseAddress member is
  // contained within this allocation range.
  uint32_t allocation_base;
  // The memory protection option when the region was initially allocated.
  uint32_t allocation_protect;
  // The size of the region beginning at the base address in which all pages
  // have identical attributes, in bytes.
  uint32_t region_size;
  // The state of the pages in the region (commit/free/reserve).
  uint32_t state;
  // The access protection of the pages in the region.
  uint32_t protect;
  // The type of pages in the region (private).
  uint32_t type;
};

union PageEntry {
  struct {
    uint32_t base_address : 20;       // in 4k pages
    uint32_t region_page_count : 20;  // in 4k pages
    uint32_t allocation_protect : 4;
    uint32_t current_protect : 4;
    uint32_t state : 2;
    uint32_t reserved : 14;
  };
  uint64_t qword;
};

class BaseHeap {
 public:
  virtual ~BaseHeap();

  uint32_t page_size() const { return page_size_; }

  virtual void Dispose();

  void DumpMap();

  virtual bool Alloc(uint32_t size, uint32_t alignment,
                     uint32_t allocation_type, uint32_t protect, bool top_down,
                     uint32_t* out_address);
  virtual bool AllocFixed(uint32_t base_address, uint32_t size,
                          uint32_t alignment, uint32_t allocation_type,
                          uint32_t protect);
  virtual bool AllocRange(uint32_t low_address, uint32_t high_address,
                          uint32_t size, uint32_t alignment,
                          uint32_t allocation_type, uint32_t protect,
                          bool top_down, uint32_t* out_address);
  virtual bool Decommit(uint32_t address, uint32_t size);
  virtual bool Release(uint32_t address, uint32_t* out_region_size = nullptr);
  virtual bool Protect(uint32_t address, uint32_t size, uint32_t protect);

  bool QueryRegionInfo(uint32_t base_address, HeapAllocationInfo* out_info);
  bool QuerySize(uint32_t address, uint32_t* out_size);
  bool QueryProtect(uint32_t address, uint32_t* out_protect);
  uint32_t GetPhysicalAddress(uint32_t address);

 protected:
  BaseHeap();

  void Initialize(uint8_t* membase, uint32_t heap_base, uint32_t heap_size,
                  uint32_t page_size);

  uint8_t* membase_;
  uint32_t heap_base_;
  uint32_t heap_size_;
  uint32_t page_size_;
  std::vector<PageEntry> page_table_;
  std::recursive_mutex heap_mutex_;
};

class VirtualHeap : public BaseHeap {
 public:
  VirtualHeap();
  ~VirtualHeap() override;

  void Initialize(uint8_t* membase, uint32_t heap_base, uint32_t heap_size,
                  uint32_t page_size);
};

class PhysicalHeap : public BaseHeap {
 public:
  PhysicalHeap();
  ~PhysicalHeap() override;

  void Initialize(uint8_t* membase, uint32_t heap_base, uint32_t heap_size,
                  uint32_t page_size, VirtualHeap* parent_heap);

  bool Alloc(uint32_t size, uint32_t alignment, uint32_t allocation_type,
             uint32_t protect, bool top_down, uint32_t* out_address) override;
  bool AllocFixed(uint32_t base_address, uint32_t size, uint32_t alignment,
                  uint32_t allocation_type, uint32_t protect) override;
  bool AllocRange(uint32_t low_address, uint32_t high_address, uint32_t size,
                  uint32_t alignment, uint32_t allocation_type,
                  uint32_t protect, bool top_down,
                  uint32_t* out_address) override;
  bool Decommit(uint32_t address, uint32_t size) override;
  bool Release(uint32_t base_address,
               uint32_t* out_region_size = nullptr) override;
  bool Protect(uint32_t address, uint32_t size, uint32_t protect) override;

 protected:
  VirtualHeap* parent_heap_;
};

class Memory {
 public:
  Memory();
  ~Memory();

  int Initialize();

  const std::wstring& file_name() const { return file_name_; }

  inline uint8_t* virtual_membase() const { return virtual_membase_; }
  inline uint8_t* TranslateVirtual(uint32_t guest_address) const {
    return virtual_membase_ + guest_address;
  };
  template <typename T>
  inline T TranslateVirtual(uint32_t guest_address) const {
    return reinterpret_cast<T>(virtual_membase_ + guest_address);
  };

  inline uint8_t* physical_membase() const { return physical_membase_; }
  inline uint8_t* TranslatePhysical(uint32_t guest_address) const {
    return physical_membase_ + (guest_address & 0x1FFFFFFF);
  }
  template <typename T>
  inline T TranslatePhysical(uint32_t guest_address) const {
    return reinterpret_cast<T>(physical_membase_ +
                               (guest_address & 0x1FFFFFFF));
  }

  inline uint64_t* reserve_address() { return &reserve_address_; }
  inline uint64_t* reserve_value() { return &reserve_value_; }

  // TODO(benvanik): make poly memory utils for these.
  void Zero(uint32_t address, uint32_t size);
  void Fill(uint32_t address, uint32_t size, uint8_t value);
  void Copy(uint32_t dest, uint32_t src, uint32_t size);
  uint32_t SearchAligned(uint32_t start, uint32_t end, const uint32_t* values,
                         size_t value_count);

  bool AddVirtualMappedRange(uint32_t virtual_address, uint32_t mask,
                             uint32_t size, void* context,
                             cpu::MMIOReadCallback read_callback,
                             cpu::MMIOWriteCallback write_callback);

  uintptr_t AddPhysicalWriteWatch(uint32_t physical_address, uint32_t length,
                                  cpu::WriteWatchCallback callback,
                                  void* callback_context, void* callback_data);
  void CancelWriteWatch(uintptr_t watch_handle);

  uint32_t SystemHeapAlloc(uint32_t size, uint32_t alignment = 0x20,
                           uint32_t system_heap_flags = kSystemHeapDefault);
  void SystemHeapFree(uint32_t address);

  BaseHeap* LookupHeap(uint32_t address);
  BaseHeap* LookupHeapByType(bool physical, uint32_t page_size);

  void DumpMap();

 private:
  int MapViews(uint8_t* mapping_base);
  void UnmapViews();

 private:
  std::wstring file_name_;
  uint32_t system_page_size_;
  uint8_t* virtual_membase_;
  uint8_t* physical_membase_;
  uint64_t reserve_address_;
  uint64_t reserve_value_;

  HANDLE mapping_;
  uint8_t* mapping_base_;
  union {
    struct {
      uint8_t* v00000000;
      uint8_t* v40000000;
      uint8_t* v7F000000;
      uint8_t* v80000000;
      uint8_t* v90000000;
      uint8_t* vA0000000;
      uint8_t* vC0000000;
      uint8_t* vE0000000;
      uint8_t* physical;
    };
    uint8_t* all_views[9];
  } views_;

  std::unique_ptr<cpu::MMIOHandler> mmio_handler_;

  struct {
    VirtualHeap v00000000;
    VirtualHeap v40000000;
    VirtualHeap v80000000;
    VirtualHeap v90000000;

    VirtualHeap physical;
    PhysicalHeap vA0000000;
    PhysicalHeap vC0000000;
    PhysicalHeap vE0000000;
  } heaps_;

  friend class BaseHeap;
};

}  // namespace xe

#endif  // XENIA_MEMORY_H_
