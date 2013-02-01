/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/dbg/indexed_content_source.h>


using namespace xe;
using namespace xe::dbg;


IndexedContentSource::IndexedContentSource() :
    ContentSource(kTypeIndexed) {
}

IndexedContentSource::~IndexedContentSource() {
}

int IndexedContentSource::DispatchRequest(Client* client,
                                          const uint8_t* data, size_t length) {
  //
  return 1;
}
