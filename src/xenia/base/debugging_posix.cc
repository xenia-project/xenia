/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/debugging.h"

#if defined(__APPLE__)
// Add this to the beginning of your source file or a common header
typedef unsigned int u_int;
typedef unsigned char u_char;
typedef unsigned short u_short;
#include <sys/sysctl.h>
#include <unistd.h>
#endif

#include <csignal>
#include <cstdarg>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

#include "xenia/base/string_buffer.h"

namespace xe {
namespace debugging {

bool IsDebuggerAttached() {
#if defined(__APPLE__)
  int mib[4];
  struct kinfo_proc info;
  size_t size = sizeof(info);

  // Initialize MIB array for sysctl call
  mib[0] = CTL_KERN;
  mib[1] = KERN_PROC;
  mib[2] = KERN_PROC_PID;
  mib[3] = getpid();

  // Call sysctl and check if we are being debugged
  if (sysctl(mib, 4, &info, &size, nullptr, 0) == -1) {
    return false;
  }

  // Check if process has a debugger attached
  return (info.kp_proc.p_flag & P_TRACED) != 0;
#else
  std::ifstream proc_status_stream("/proc/self/status");
  if (!proc_status_stream.is_open()) {
    return false;
  }
  std::string line;
  while (std::getline(proc_status_stream, line)) {
    std::istringstream line_stream(line);
    std::string key;
    line_stream >> key;
    if (key == "TracerPid:") {
      uint32_t tracer_pid;
      line_stream >> tracer_pid;
      return tracer_pid != 0;
    }
  }
  return false;
#endif
}

void Break() {
  static std::once_flag flag;
  std::call_once(flag, []() {
    // Install handler for sigtrap only once
    std::signal(SIGTRAP, [](int) {
      // Forward signal to default handler after being caught
      std::signal(SIGTRAP, SIG_DFL);
    });
  });
  std::raise(SIGTRAP);
}

namespace internal {
void DebugPrint(const char* s) { std::clog << s << std::endl; }
}  // namespace internal

}  // namespace debugging
}  // namespace xe
