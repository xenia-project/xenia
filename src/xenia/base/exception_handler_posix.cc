/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/exception_handler.h"

namespace xe {

// TODO(DrChat): Exception handling on linux.
void ExceptionHandler::Install(Handler fn, void* data) {}
void ExceptionHandler::Uninstall(Handler fn, void* data) {}

}  // namespace xe