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


ContentSource::ContentSource(Type type) :
    type_(type) {
}

ContentSource::~ContentSource() {
}


ContentSource::Type ContentSource::type() {
  return type_;
}
