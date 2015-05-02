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

#include <mutex>

#include "xenia/base/main.h"
#include "xenia/base/math.h"
#include "xenia/base/threading.h"

DEFINE_bool(fast_stdout, false,
            "Don't lock around stdout/stderr. May introduce weirdness.");
DEFINE_bool(flush_stdout, true, "Flush stdout after each log line.");
DEFINE_bool(log_filenames, false,
            "Log filenames/line numbers in log statements.");

namespace xe {

std::mutex log_lock;

void format_log_line(char* buffer, size_t buffer_count, const char* file_path,
                     const uint32_t line_number, const char level_char,
                     const char* fmt, va_list args) {
  char* buffer_ptr;
  if (FLAGS_log_filenames) {
    // Strip out just the filename from the path.
    const char* filename = strrchr(file_path, xe::path_separator);
    if (filename) {
      // Slash - skip over it.
      filename++;
    } else {
      // No slash, entire thing is filename.
      filename = file_path;
    }

    // Format string - add a trailing newline if required.
    const char* outfmt = "%c> %.2X %s:%d: ";
    buffer_ptr = buffer + snprintf(buffer, buffer_count - 1, outfmt, level_char,
                                   xe::threading::current_thread_id(), filename,
                                   line_number);
  } else {
    buffer_ptr = buffer;
    *(buffer_ptr++) = level_char;
    *(buffer_ptr++) = '>';
    *(buffer_ptr++) = ' ';
    buffer_ptr +=
        sprintf(buffer_ptr, "%.4X", xe::threading::current_thread_id());
    *(buffer_ptr++) = ' ';
  }

  // Scribble args into the print buffer.
  buffer_ptr = buffer_ptr + vsnprintf(buffer_ptr,
                                      buffer_count - (buffer_ptr - buffer) - 1,
                                      fmt, args);

  // Add a trailing newline.
  if (buffer_ptr[-1] != '\n') {
    buffer_ptr[0] = '\n';
    buffer_ptr[1] = 0;
  }
}

thread_local char log_buffer[2048];

void log_line(const char* file_path, const uint32_t line_number,
              const char level_char, const char* fmt, ...) {
  // SCOPE_profile_cpu_i("emu", "log_line");

  va_list args;
  va_start(args, fmt);
  format_log_line(log_buffer, xe::countof(log_buffer), file_path, line_number,
                  level_char, fmt, args);
  va_end(args);

  if (!FLAGS_fast_stdout) {
    log_lock.lock();
  }
#if 0  // defined(OutputDebugString)
  OutputDebugStringA(log_buffer);
#else
  fprintf(stdout, "%s", log_buffer);
  if (FLAGS_flush_stdout) {
    fflush(stdout);
  }
#endif  // OutputDebugString
  if (!FLAGS_fast_stdout) {
    log_lock.unlock();
  }
}

void handle_fatal(const char* file_path, const uint32_t line_number,
                  const char* fmt, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, fmt);
  format_log_line(buffer, xe::countof(buffer), file_path, line_number, 'X', fmt,
                  args);
  va_end(args);

  if (!FLAGS_fast_stdout) {
    log_lock.lock();
  }
#if defined(OutputDebugString)
  OutputDebugStringA(buffer);
#else
  fprintf(stderr, "%s", buffer);
  fflush(stderr);
#endif  // OutputDebugString
  if (!FLAGS_fast_stdout) {
    log_lock.unlock();
  }

#if XE_PLATFORM_WIN32
  if (!xe::has_console_attached()) {
    MessageBoxA(NULL, buffer, "Xenia Error",
                MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
  }
#endif  // WIN32

  exit(1);
}

}  // namespace xe
