/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/exception_handler.h"

#include "xenia/base/platform_win.h"

namespace xe {
std::vector<ExceptionHandler::Handler> ExceptionHandler::handlers_;

LONG CALLBACK ExceptionHandlerCallback(PEXCEPTION_POINTERS ex_info) {
  // Visual Studio SetThreadName
  if (ex_info->ExceptionRecord->ExceptionCode == 0x406D1388) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  auto code = ex_info->ExceptionRecord->ExceptionCode;
  ExceptionHandler::Info info;
  info.pc = ex_info->ContextRecord->Rip;
  info.thread_context = ex_info->ContextRecord;

  switch (code) {
    case STATUS_ACCESS_VIOLATION:
      info.code = ExceptionHandler::Info::kAccessViolation;
      info.fault_address = ex_info->ExceptionRecord->ExceptionInformation[1];
      break;
  }

  // Only call a handler if we support this type of exception.
  if (info.code != ExceptionHandler::Info::kInvalidException) {
    for (auto handler : ExceptionHandler::handlers()) {
      if (handler(&info)) {
        // Exception handled.
        return EXCEPTION_CONTINUE_EXECUTION;
      }
    }
  }

  return EXCEPTION_CONTINUE_SEARCH;
}

bool ExceptionHandler::Initialize() {
  AddVectoredExceptionHandler(0, ExceptionHandlerCallback);

  // TODO: Do we need a continue handler if a debugger is attached?
  return true;
}

uint32_t ExceptionHandler::Install(std::function<bool(Info* ex_info)> fn) {
  handlers_.push_back(fn);

  // TODO: ID support!
  return 0;
}
}