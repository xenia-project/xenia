#include "xenia/base/debugging.h"

// Add this to the beginning of your source file or a common header
typedef unsigned int u_int;
typedef unsigned char u_char;
typedef unsigned short u_short;

#include <csignal>
#include <cstdarg>
#include <iostream>
#include <mutex>
#include <sstream>
#include <sys/sysctl.h>
#include <unistd.h>

#include "xenia/base/string_buffer.h"

namespace xe {
namespace debugging {

bool IsDebuggerAttached() {
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
}

void Break() {
  static std::once_flag flag;
  std::call_once(flag, []() {
    // Install handler for SIGTRAP only once
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
