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

#include <xenia/core/ref.h>
#include <xenia/core/run_loop.h>
#include <xenia/core/socket.h>
#include <xenia/memory.h>

// TODO(benvanik): remove.
#define XEFAIL() goto XECLEANUP
#define XEEXPECTZERO(expr) \
  if ((expr) != 0) {       \
    goto XECLEANUP;        \
  }
#define XEEXPECTNOTNULL(expr) \
  if ((expr) == NULL) {       \
    goto XECLEANUP;           \
  }

#endif  // XENIA_CORE_H_
