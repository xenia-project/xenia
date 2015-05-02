/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/debugging.h"

#include <sys/sysctl.h>
#include <unistd.h>

namespace xe {
namespace debugging {

bool IsDebuggerAttached() {
  // https://developer.apple.com/library/mac/qa/qa1361/_index.html
  kinfo_proc info;
  info.kp_proc.p_flag = 0;
  int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
  size_t size = sizeof(info);
  sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, nullptr, 0);
  return (info.kp_proc.p_flag & P_TRACED) != 0;
}

// TODO(benvanik): find a more reliable way.
void Break() {
  // __asm__("int $3");
  __builtin_debugtrap();
}

}  // namespace debugging
}  // namespace xe
