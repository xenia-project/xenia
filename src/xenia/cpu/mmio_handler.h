/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_MMIO_HANDLER_H_
#define XENIA_CPU_MMIO_HANDLER_H_

#include <list>
#include <memory>
#include <mutex>
#include <vector>

#include "xenia/base/mutex.h"

namespace xe {
namespace cpu {

typedef uint64_t (*MMIOReadCallback)(void* ppc_context, void* callback_context,
                                     uint32_t addr);
typedef void (*MMIOWriteCallback)(void* ppc_context, void* callback_context,
                                  uint32_t addr, uint64_t value);

typedef void (*WriteWatchCallback)(void* context_ptr, void* data_ptr,
                                   uint32_t address);

struct MMIORange {
  uint32_t address;
  uint32_t mask;
  uint32_t size;
  void* callback_context;
  MMIOReadCallback read;
  MMIOWriteCallback write;
};

// NOTE: only one can exist at a time!
class MMIOHandler {
 public:
  virtual ~MMIOHandler();

  static std::unique_ptr<MMIOHandler> Install(uint8_t* virtual_membase,
                                              uint8_t* physical_membase);
  static MMIOHandler* global_handler() { return global_handler_; }

  bool RegisterRange(uint32_t virtual_address, uint32_t mask, uint32_t size,
                     void* context, MMIOReadCallback read_callback,
                     MMIOWriteCallback write_callback);
  MMIORange* LookupRange(uint32_t virtual_address);

  bool CheckLoad(uint32_t virtual_address, uint64_t* out_value);
  bool CheckStore(uint32_t virtual_address, uint64_t value);

  uintptr_t AddPhysicalWriteWatch(uint32_t guest_address, size_t length,
                                  WriteWatchCallback callback,
                                  void* callback_context, void* callback_data);
  void CancelWriteWatch(uintptr_t watch_handle);

 public:
  bool HandleAccessFault(void* thread_state, uint64_t fault_address);

 protected:
  struct WriteWatchEntry {
    uint32_t address;
    uint32_t length;
    WriteWatchCallback callback;
    void* callback_context;
    void* callback_data;
  };

  MMIOHandler(uint8_t* virtual_membase, uint8_t* physical_membase)
      : virtual_membase_(virtual_membase),
        physical_membase_(physical_membase) {}

  virtual bool Initialize() = 0;

  void ClearWriteWatch(WriteWatchEntry* entry);
  bool CheckWriteWatch(void* thread_state, uint64_t fault_address);

  virtual uint64_t GetThreadStateRip(void* thread_state_ptr) = 0;
  virtual void SetThreadStateRip(void* thread_state_ptr, uint64_t rip) = 0;
  virtual uint64_t* GetThreadStateRegPtr(void* thread_state_ptr,
                                         int32_t be_reg_index) = 0;

  uint8_t* virtual_membase_;
  uint8_t* physical_membase_;

  std::vector<MMIORange> mapped_ranges_;

  // TODO(benvanik): data structure magic.
  xe::mutex write_watch_mutex_;
  std::list<WriteWatchEntry*> write_watches_;

  static MMIOHandler* global_handler_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_MMIO_HANDLER_H_
