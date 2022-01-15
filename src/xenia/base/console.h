/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_CONSOLE_H_
#define XENIA_BASE_CONSOLE_H_

namespace xe {

// Returns true if there is a user-visible console attached to receive stdout.
bool has_console_attached();

void AttachConsole();

}  // namespace xe

#endif  // XENIA_BASE_CONSOLE_H_
