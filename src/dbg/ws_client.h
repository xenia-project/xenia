/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_DBG_WS_CLIENT_H_
#define XENIA_KERNEL_DBG_WS_CLIENT_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/dbg/client.h>


namespace xe {
namespace dbg {


class WsClient : public Client {
public:
  WsClient(int socket_id);
  virtual ~WsClient();

  virtual void Write(const uint8_t** buffers, const size_t* lengths,
                     size_t count);

protected:
  int socket_id_;
};


}  // namespace dbg
}  // namespace xe


#endif  // XENIA_KERNEL_DBG_WS_CLIENT_H_
