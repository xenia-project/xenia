/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/debug/protocol.h>


using namespace xe;
using namespace xe::debug;


Protocol::Protocol(DebugServer* debug_server) :
    debug_server_(debug_server) {
}

Protocol::~Protocol() {
}
