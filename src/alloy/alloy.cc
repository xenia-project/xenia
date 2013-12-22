/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/alloy.h>
#include <alloy/alloy-private.h>

using namespace alloy;


#if DEBUG
#define DEFAULT_DEBUG_FLAG true
#else
#define DEFAULT_DEBUG_FLAG false
#endif

DEFINE_bool(debug, DEFAULT_DEBUG_FLAG,
    "Allow debugging and retain debug information.");
