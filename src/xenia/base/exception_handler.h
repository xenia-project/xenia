/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_EXCEPTION_HANDLER_H_
#define XENIA_BASE_EXCEPTION_HANDLER_H_

#include <functional>
#include <vector>

namespace xe {
class ExceptionHandler {
 public:
  struct Info {
    enum {
      kInvalidException = 0,
      kAccessViolation,
    } code = kInvalidException;

    uint64_t pc = 0;  // Program counter address. RIP on x64.
    uint64_t fault_address =
        0;  // In case of AV, address that was read from/written to.

    void* thread_context = nullptr;  // Platform-specific thread context info.
  };
  typedef std::function<bool(Info* ex_info)> Handler;

  // Static initialization. Only call this once!
  static bool Initialize();

  // Install an exception handler. Returns an ID which you can save to remove
  // this later. This will install the exception handler in the last place
  // on Windows.
  static uint32_t Install(Handler fn);
  static bool Remove(uint32_t id);

  static const std::vector<Handler>& handlers() { return handlers_; }

 private:
  static std::vector<Handler> handlers_;
};
};  // namespace xe

#endif  // XENIA_BASE_EXCEPTION_HANDLER_H_