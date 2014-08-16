/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <poly/threading.h>

#include <poly/platform.h>

namespace poly {
namespace threading {

uint64_t ticks() {
  LARGE_INTEGER counter;
  uint64_t time = 0;
  if (QueryPerformanceCounter(&counter)) {
    time = counter.QuadPart;
  }
  return time;
}

uint32_t current_thread_id() {
  return static_cast<uint32_t>(GetCurrentThreadId());
}

// http://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx
#pragma pack(push, 8)
struct THREADNAME_INFO {
  DWORD dwType;      // Must be 0x1000.
  LPCSTR szName;     // Pointer to name (in user addr space).
  DWORD dwThreadID;  // Thread ID (-1=caller thread).
  DWORD dwFlags;     // Reserved for future use, must be zero.
};
#pragma pack(pop)

void set_name(DWORD thread_id, const std::string& name) {
  if (!IsDebuggerPresent()) {
    return;
  }
  THREADNAME_INFO info;
  info.dwType = 0x1000;
  info.szName = name.c_str();
  info.dwThreadID = thread_id;
  info.dwFlags = 0;
  __try {
    RaiseException(0x406D1388, 0, sizeof(info) / sizeof(ULONG_PTR),
                   reinterpret_cast<ULONG_PTR*>(&info));
  }
  __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void set_name(const std::string& name) {
  set_name(static_cast<DWORD>(-1), name);
}

void set_name(std::thread::native_handle_type handle, const std::string& name) {
  set_name(GetThreadId(handle), name);
}

void Yield() { SwitchToThread(); }

void Sleep(std::chrono::microseconds duration) {
  if (duration.count() < 100) {
    SwitchToThread();
  } else {
    ::Sleep(static_cast<DWORD>(duration.count() / 1000));
  }
}

}  // namespace threading
}  // namespace poly
