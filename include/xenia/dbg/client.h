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

  virtual int Setup() = 0;

  void Write(uint8_t* buffer, size_t length);
  virtual void Write(uint8_t** buffers, size_t* lengths, size_t count) = 0;

protected:
};


}  // namespace dbg
}  // namespace xe


#endif  // XENIA_KERNEL_DBG_CLIENT_H_
