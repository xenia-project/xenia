/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_SERVER_MI_MI_PROTOCOL_H_
#define XENIA_DEBUG_SERVER_MI_MI_PROTOCOL_H_

#include <cstdint>

namespace xe {
namespace debug {
namespace server {
namespace mi {

enum class RecordToken : char {
  kResult = '^',
  kAsyncExec = '*',
  kAsyncStatus = '+',
  kAsyncNotify = '=',
};

enum class StreamToken : char {
  kConsole = '~',
  kTarget = '@',
  kLog = '&',
};

enum class ResultClass {
  kDone,
  kRunning,
  kConnected,
  kError,
  kExit,
};

}  // namespace mi
}  // namespace server
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_SERVER_MI_MI_PROTOCOL_H_
