/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/dbg/client.h>


using namespace xe;
using namespace xe::dbg;


Client::Client() {
}

Client::~Client() {
}

void Client::Write(const uint8_t* buffer, const size_t length) {
  const uint8_t* buffers[] = {buffer};
  const size_t lengths[] = {length};
  Write(buffers, lengths, 1);
}
