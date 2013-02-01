/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_DBG_CLIENT_H_
#define XENIA_KERNEL_DBG_CLIENT_H_

#include <xenia/common.h>
#include <xenia/core.h>


namespace xe {
namespace dbg {


class Client {
public:
  Client();
  virtual ~Client();

  void Write(const uint8_t* buffer, const size_t length);
  virtual void Write(const uint8_t** buffers, const size_t* lengths,
                     size_t count) = 0;

protected:
};


}  // namespace dbg
}  // namespace xe


#endif  // XENIA_KERNEL_DBG_CLIENT_H_
