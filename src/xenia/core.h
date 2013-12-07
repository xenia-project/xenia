/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_H_
#define XENIA_CORE_H_

#include <xenia/common.h>

#include <alloy/memory.h>
namespace xe {
  using Memory = alloy::Memory;
}  // namespace xe

#include <xenia/core/crc32.h>
#include <xenia/core/file.h>
#include <xenia/core/hash.h>
#include <xenia/core/mmap.h>
#include <xenia/core/mutex.h>
#include <xenia/core/pal.h>
#include <xenia/core/ref.h>
#include <xenia/core/run_loop.h>
#include <xenia/core/socket.h>
#include <xenia/core/thread.h>
#include <xenia/core/window.h>

#if XE_PLATFORM(WIN32)
#include <xenia/core/win32_window.h>
#endif  // WIN32

#endif  // XENIA_CORE_H_
