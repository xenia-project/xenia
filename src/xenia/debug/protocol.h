/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2015 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#ifndef XENIA_DEBUG_PROTOCOL_H_
#define XENIA_DEBUG_PROTOCOL_H_

namespace xe {
namespace debug {
namespace protocol {

template <typename T>
class Packet {
  public:

  protected:
    int type_;
};

} // namespace protocol
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_PROTOCOL_H_
