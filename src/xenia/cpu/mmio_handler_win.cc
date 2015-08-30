/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/mmio_handler.h"

#include "xenia/base/platform_win.h"
#include "xenia/profiling.h"

namespace xe {
void CrashDump();
}  // namespace xe

namespace xe {
namespace cpu {

LONG CALLBACK MMIOExceptionHandler(PEXCEPTION_POINTERS ex_info);

class WinMMIOHandler : public MMIOHandler {
 public:
  WinMMIOHandler(uint8_t* virtual_membase, uint8_t* physical_membase)
      : MMIOHandler(virtual_membase, physical_membase) {}
  ~WinMMIOHandler() override;

 protected:
  bool Initialize() override;

  uint64_t GetThreadStateRip(void* thread_state_ptr) override;
  void SetThreadStateRip(void* thread_state_ptr, uint64_t rip) override;
  uint64_t* GetThreadStateRegPtr(void* thread_state_ptr,
                                 int32_t reg_index) override;
};

std::unique_ptr<MMIOHandler> CreateMMIOHandler(uint8_t* virtual_membase,
                                               uint8_t* physical_membase) {
  return std::make_unique<WinMMIOHandler>(virtual_membase, physical_membase);
}

bool WinMMIOHandler::Initialize() {
  // If there is a debugger attached the normal exception handler will not
  // fire and we must instead add the continue handler.
  AddVectoredExceptionHandler(1, MMIOExceptionHandler);
  if (IsDebuggerPresent()) {
    // TODO(benvanik): is this really required?
    // AddVectoredContinueHandler(1, MMIOExceptionHandler);
  }
  return true;
}

WinMMIOHandler::~WinMMIOHandler() {
  // Remove exception handlers.
  RemoveVectoredExceptionHandler(MMIOExceptionHandler);
  RemoveVectoredContinueHandler(MMIOExceptionHandler);
}

// Handles potential accesses to mmio. We look for access violations to
// addresses in our range and call into the registered handlers, if any.
// If there are none, we continue.
LONG CALLBACK MMIOExceptionHandler(PEXCEPTION_POINTERS ex_info) {
  // Fast path for SetThreadName.
  if (ex_info->ExceptionRecord->ExceptionCode == 0x406D1388) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // Disabled because this can cause odd issues during handling.
  // SCOPE_profile_cpu_i("cpu", "MMIOExceptionHandler");

  // http://msdn.microsoft.com/en-us/library/ms679331(v=vs.85).aspx
  // http://msdn.microsoft.com/en-us/library/aa363082(v=vs.85).aspx
  auto code = ex_info->ExceptionRecord->ExceptionCode;
  if (code == STATUS_ACCESS_VIOLATION) {
    auto fault_address = ex_info->ExceptionRecord->ExceptionInformation[1];
    if (MMIOHandler::global_handler()->HandleAccessFault(ex_info->ContextRecord,
                                                         fault_address)) {
      // Handled successfully - RIP has been updated and we can continue.
      return EXCEPTION_CONTINUE_EXECUTION;
    } else {
      // Failed to handle; continue search for a handler (and die if no other
      // handler is found).
      xe::CrashDump();
      return EXCEPTION_CONTINUE_SEARCH;
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

uint64_t WinMMIOHandler::GetThreadStateRip(void* thread_state_ptr) {
  auto context = reinterpret_cast<LPCONTEXT>(thread_state_ptr);
  return context->Rip;
}

void WinMMIOHandler::SetThreadStateRip(void* thread_state_ptr, uint64_t rip) {
  auto context = reinterpret_cast<LPCONTEXT>(thread_state_ptr);
  context->Rip = rip;
}

uint64_t* WinMMIOHandler::GetThreadStateRegPtr(void* thread_state_ptr,
                                               int32_t reg_index) {
  auto context = reinterpret_cast<LPCONTEXT>(thread_state_ptr);
  // Register indices line up with the CONTEXT structure format.
  return &context->Rax + reg_index;
}

}  // namespace cpu
}  // namespace xe
