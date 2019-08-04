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

 protected:
  MMIOHandler(uint8_t* virtual_membase, uint8_t* physical_membase,
              uint8_t* membase_end,
              AccessViolationCallback access_violation_callback,
              void* access_violation_callback_context);

  static bool ExceptionCallbackThunk(Exception* ex, void* data);
  bool ExceptionCallback(Exception* ex);

  uint8_t* virtual_membase_;
  uint8_t* physical_membase_;
  uint8_t* memory_end_;

  std::vector<MMIORange> mapped_ranges_;

  AccessViolationCallback access_violation_callback_;
  void* access_violation_callback_context_;

  static MMIOHandler* global_handler_;

  xe::global_critical_region global_critical_region_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_MMIO_HANDLER_H_
