/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/dbg/content_source.h>


using namespace xe;
using namespace xe::dbg;


ContentSource::ContentSource(Debugger* debugger, uint32_t source_id) :
    debugger_(debugger), source_id_(source_id) {
}

ContentSource::~ContentSource() {
}

uint32_t ContentSource::source_id() {
  return source_id_;
}
