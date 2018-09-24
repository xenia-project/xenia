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
typedef void (*PhysicalWriteWatchCallback)(void* context_ptr,
                                           uint32_t page_first,
                                           uint32_t page_last);

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

  enum WatchType {
    kWatchInvalid = 0,
    kWatchWrite = 1,
    kWatchReadWrite = 2,
  };

  static std::unique_ptr<MMIOHandler> Install(uint8_t* virtual_membase,
                                              uint8_t* physical_membase,
                                              uint8_t* membase_end);
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

  // Physical memory write watching, allowing subsystems to invalidate cached
  // data that depends on memory contents.
  //
  // Placing a watch simply marks the pages (of the system page size) as
  // watched, individual watched ranges (or which specific subscribers are
  // watching specific pages) are not stored. Because of this, callbacks may be
  // triggered multiple times for a single range, and for any watched page every
  // registered callbacks is triggered. This is a very simple one-shot method
  // for use primarily for cache invalidation - there may be spurious firing,
  // for example, if the game only changes the protection level without writing
  // anything.
  //
  // A range of pages can be watched at any time, but pages are only unwatched
  // when watches are triggered (since multiple subscribers can depend on the
  // same memory, and one subscriber shouldn't interfere with another).
  //
  // Callbacks can be triggered for one page (if the guest just stores words) or
  // for multiple pages (for file reading, protection level changes).
  //
  // Only guest physical memory mappings are watched - the host-only mapping is
  // not protected so it can be used to bypass the write protection (for file
  // reads, for example - in this case, watches are triggered manually).
  //
  // Ranges passed to ProtectAndWatchPhysicalMemory must not contain read-only
  // or inaccessible pages - this must be checked externally! Otherwise the MMIO
  // handler will make them read-only, but when a read is attempted, it will
  // make them read-write!
  //
  // IMPORTANT NOTE: When a watch is triggered, the watched page is unprotected
  // ***ONLY IN THE HEAP WHERE THE ADDRESS IS LOCATED***! Since different
  // virtual memory mappings of physical memory can have different protection
  // levels for the same pages, and watches must not be placed on read-only or
  // totally inaccessible pages, there are significant difficulties with
  // synchronizing all the three ranges.
  //
  // TODO(Triang3l): Allow the callbacks to unwatch regions larger than one page
  // (for instance, 64 KB) so there are less access violations. All callbacks
  // must agree to unwatch larger ranges because in some cases (like regions
  // near the locations that render targets have been resolved to) it is
  // necessary to invalidate only a single page and none more.
  void* RegisterPhysicalWriteWatch(PhysicalWriteWatchCallback callback,
                                   void* callback_context);
  void UnregisterPhysicalWriteWatch(void* watch_handle);
  // Force-protects the range in ***ONE SPECIFIC HEAP***, either 0xA0000000,
  // 0xC0000000 or 0xE0000000, depending on the higher bits of the address.
  void ProtectAndWatchPhysicalMemory(uint32_t physical_address_and_heap,
                                     uint32_t length);

  // Fires and clears any write watches that overlap this range in one heap.
  // Unprotecting can be inhibited if this is called right before applying
  // different protection to the same range.
  void InvalidateRange(uint32_t physical_address_and_heap, uint32_t length,
                       bool unprotect = true);

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

  struct PhysicalWriteWatchEntry {
    PhysicalWriteWatchCallback callback;
    void* callback_context;
  };

  MMIOHandler(uint8_t* virtual_membase, uint8_t* physical_membase,
              uint8_t* membase_end);

  static bool ExceptionCallbackThunk(Exception* ex, void* data);
  bool ExceptionCallback(Exception* ex);

  void FireAccessWatch(AccessWatchEntry* entry);
  void ClearAccessWatch(AccessWatchEntry* entry);
  bool CheckAccessWatch(uint32_t guest_address, uint32_t guest_heap_address);

  uint32_t system_page_size_log2_;

  uint8_t* virtual_membase_;
  uint8_t* physical_membase_;
  uint8_t* memory_end_;

  std::vector<MMIORange> mapped_ranges_;

  xe::global_critical_region global_critical_region_;
  // TODO(benvanik): data structure magic.
  std::list<AccessWatchEntry*> access_watches_;
  std::vector<PhysicalWriteWatchEntry*> physical_write_watches_;
  // For each page, there are 4 bits (16 pages in each word):
  // 0 - whether the page is protected in A0000000.
  // 1 - whether the page is protected in C0000000.
  // 2 - whether the page is protected in E0000000.
  // 3 - unused, always zero.
  std::vector<uint64_t> physical_write_watched_pages_;

  static MMIOHandler* global_handler_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_MMIO_HANDLER_H_
