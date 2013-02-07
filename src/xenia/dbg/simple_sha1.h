/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DBG_SIMPLE_SHA1_H_
#define XENIA_DBG_SIMPLE_SHA1_H_

#include <xenia/common.h>
#include <xenia/core.h>


namespace xe {
namespace dbg {


// This is a (likely) slow SHA1 designed for use on small values such as
// Websocket security keys. If we need something more complex it'd be best
// to use a real library.
void SHA1(const uint8_t* data, size_t length, uint8_t out_hash[20]);


}  // namespace dbg
}  // namespace xe


#endif  // XENIA_DBG_SIMPLE_SHA1_H_
