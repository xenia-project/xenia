/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"

#include <gflags/gflags.h>

#include <cstdarg>
#include <mutex>

#include "xenia/base/main.h"
#include "xenia/base/math.h"
#include "xenia/base/threading.h"

// For MessageBox:
// TODO(benvanik): generic API? logging_win.cc?
#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif  // XE_PLATFORM_WIN32

DEFINE_bool(fast_stdout, false,
            "Don't lock around stdout/stderr. May introduce weirdness.");
DEFINE_bool(flush_stdout, true, "Flush stdout after each log line.");

namespace xe {

std::mutex log_lock;

thread_local std::vector<char> log_buffer(16 * 1024);

void format_log_line(char* buffer, size_t buffer_capacity,
                     const char level_char, const char* fmt, va_list args) {
  char* buffer_ptr;
  buffer_ptr = buffer;
  *(buffer_ptr++) = level_char;
  *(buffer_ptr++) = '>';
  *(buffer_ptr++) = ' ';
  buffer_ptr += sprintf(buffer_ptr, "%.4X", xe::threading::current_thread_id());
  *(buffer_ptr++) = ' ';

  // Scribble args into the print buffer.
  size_t remaining_capacity = buffer_capacity - (buffer_ptr - buffer) - 3;
  size_t chars_written = vsnprintf(buffer_ptr, remaining_capacity, fmt, args);
  if (chars_written >= remaining_capacity) {
    buffer_ptr += remaining_capacity - 1;
  } else {
    buffer_ptr += chars_written;
  }

  // Add a trailing newline.
  if (buffer_ptr[-1] != '\n') {
    buffer_ptr[0] = '\n';
    buffer_ptr[1] = 0;
  }
}

void log_line(const char level_char, const char* fmt, ...) {
  // SCOPE_profile_cpu_i("emu", "log_line");

  va_list args;
  va_start(args, fmt);
  format_log_line(log_buffer.data(), log_buffer.capacity(), level_char, fmt,
                  args);
  va_end(args);

  if (!FLAGS_fast_stdout) {
    log_lock.lock();
  }
#if 0  // defined(OutputDebugString)
  OutputDebugStringA(log_buffer.data());
#else
  fprintf(stdout, "%s", log_buffer.data());
  if (FLAGS_flush_stdout) {
    fflush(stdout);
  }
#endif  // OutputDebugString
  if (!FLAGS_fast_stdout) {
    log_lock.unlock();
  }
}

void handle_fatal(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  format_log_line(log_buffer.data(), log_buffer.capacity(), 'X', fmt, args);
  va_end(args);

  if (!FLAGS_fast_stdout) {
    log_lock.lock();
  }
#if defined(OutputDebugString)
  OutputDebugStringA(log_buffer.data());
#else
  fprintf(stderr, "%s", log_buffer.data());
  fflush(stderr);
#endif  // OutputDebugString
  if (!FLAGS_fast_stdout) {
    log_lock.unlock();
  }

#if XE_PLATFORM_WIN32
  if (!xe::has_console_attached()) {
    MessageBoxA(NULL, log_buffer.data(), "Xenia Error",
                MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
  }
#endif  // WIN32

  exit(1);
}

}  // namespace xe
