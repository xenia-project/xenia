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
#include <vector>

#include "xenia/base/mutex.h"

namespace xe {
class Exception;
class X64Context;
}  // namespace xe

namespace xe {
namespace cpu {

typedef uint32_t (*MMIOReadCallback)(void* ppc_context, void* callback_context,
                                     uint32_t addr);
typedef void (*MMIOWriteCallback)(void* ppc_context, void* callback_context,
                                  uint32_t addr, uint32_t value);
typedef void (*AccessWatchCallback)(void* context_ptr, void* data_ptr,
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

  typedef bool (*AccessViolationCallback)(void* context, size_t host_address,
                                          bool is_write);

  enum WatchType {
    kWatchInvalid = 0,
    kWatchWrite = 1,
    kWatchReadWrite = 2,
  };

  // access_violation_callback is called in global_critical_region, so if
  // multiple threads trigger an access violation in the same page, the callback
  // will be called only once.
  static std::unique_ptr<MMIOHandler> Install(
      uint8_t* virtual_membase, uint8_t* physical_membase, uint8_t* membase_end,
      AccessViolationCallback access_violation_callback,
      void* access_violation_callback_context);
  static MMIOHandler* global_handler() { return global_handler_; }

  bool RegisterRange(uint32_t virtual_address, uint32_t mask, uint32_t size,
                     void* context, MMIOReadCallback read_callback,
                     MMIOWriteCallback write_callback);
  MMIORange* LookupRange(uint32_t virtual_address);

  bool CheckLoad(uint32_t virtual_address, uint32_t* out_value);
  bool CheckStore(uint32_t virtual_address, uint32_t value);

  // Memory watches: These are one-shot alarms that fire a callback (in the
  // context of the thread that caused the callback) when a memory range is
  // either written to or read from, depending on the watch type. These fire as
  // soon as a read/write happens, and only fire once.
  // These watches may be spuriously fired if memory is accessed nearby.
  // TODO(Triang3l): This is legacy currently used only to support the old
  // Vulkan graphics layer. Remove and use WatchPhysicalMemoryWrite instead.
  uintptr_t AddPhysicalAccessWatch(uint32_t guest_address, size_t length,
                                   WatchType type, AccessWatchCallback callback,
                                   void* callback_context, void* callback_data);
  void CancelAccessWatch(uintptr_t watch_handle);

  // Fires and clears any write watches that overlap this range in one heap.
  // Unprotecting can be inhibited if this is called right before applying
  // different protection to the same range.
  void InvalidateRange(uint32_t physical_address_and_heap, uint32_t length);

  // Returns true if /all/ of this range is watched.
  // TODO(Triang3l): Remove when legacy watches are removed.
  bool IsRangeWatched(uint32_t physical_address, size_t length);

 protected:
  struct AccessWatchEntry {
    uint32_t address;
    uint32_t length;
    WatchType type;
    AccessWatchCallback callback;
    void* callback_context;
    void* callback_data;
  };

  MMIOHandler(uint8_t* virtual_membase, uint8_t* physical_membase,
              uint8_t* membase_end,
              AccessViolationCallback access_violation_callback,
              void* access_violation_callback_context);

  static bool ExceptionCallbackThunk(Exception* ex, void* data);
  bool ExceptionCallback(Exception* ex);

  void FireAccessWatch(AccessWatchEntry* entry);
  void ClearAccessWatch(AccessWatchEntry* entry);
  bool CheckAccessWatch(uint32_t guest_address, uint32_t guest_heap_address);

  uint8_t* virtual_membase_;
  uint8_t* physical_membase_;
  uint8_t* memory_end_;

  std::vector<MMIORange> mapped_ranges_;

  AccessViolationCallback access_violation_callback_;
  void* access_violation_callback_context_;

  xe::global_critical_region global_critical_region_;
  // TODO(benvanik): data structure magic.
  std::list<AccessWatchEntry*> access_watches_;

  static MMIOHandler* global_handler_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_MMIO_HANDLER_H_
