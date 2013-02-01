/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_DBG_CONTENT_SOURCE_H_
#define XENIA_KERNEL_DBG_CONTENT_SOURCE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/dbg/client.h>


namespace xe {
namespace dbg {


class ContentSource {
public:
  enum Type {
    kTypeIndexed,
  };

  ContentSource(Type type);
  virtual ~ContentSource();

  Type type();

  virtual int DispatchRequest(Client* client,
                              const uint8_t* data, size_t length) = 0;

protected:
  Type  type_;
};


}  // namespace dbg
}  // namespace xe


#endif  // XENIA_KERNEL_DBG_CONTENT_SOURCE_H_
