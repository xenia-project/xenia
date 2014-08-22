/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <poly/logging.h>

#include <mutex>

#include <gflags/gflags.h>
#include <poly/main.h>
#include <poly/math.h>

DEFINE_bool(fast_stdout, false,
            "Don't lock around stdout/stderr. May introduce weirdness.");
DEFINE_bool(log_filenames, false,
            "Log filenames/line numbers in log statements.");

namespace poly {

std::mutex log_lock;

void format_log_line(char* buffer, size_t buffer_count, const char* file_path,
                     const uint32_t line_number, const char level_char,
                     const char* fmt, va_list args) {
  char* buffer_ptr;
  if (FLAGS_log_filenames) {
    // Strip out just the filename from the path.
    const char* filename = strrchr(file_path, poly::path_separator);
    if (filename) {
      // Slash - skip over it.
      filename++;
    } else {
      // No slash, entire thing is filename.
      filename = file_path;
    }

    // Format string - add a trailing newline if required.
    const char* outfmt = "%c> %s:%d: ";
    buffer_ptr = buffer + snprintf(buffer, buffer_count - 1, outfmt, level_char,
                                   filename, line_number);
  } else {
    buffer_ptr = buffer;
    *(buffer_ptr++) = level_char;
    *(buffer_ptr++) = '>';
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

void log_line(const char* file_path, const uint32_t line_number,
              const char level_char, const char* fmt, ...) {
  // SCOPE_profile_cpu_i("emu", "log_line");

  char buffer[2048];
  va_list args;
  va_start(args, fmt);
  format_log_line(buffer, poly::countof(buffer), file_path, line_number,
                  level_char, fmt, args);
  va_end(args);

  if (!FLAGS_fast_stdout) {
    log_lock.lock();
  }
#if 0  // defined(OutputDebugString)
  OutputDebugStringA(buffer);
#else
  fprintf(stdout, buffer);
  fflush(stdout);
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
  format_log_line(buffer, poly::countof(buffer), file_path, line_number, 'X',
                  fmt, args);
  va_end(args);

  if (!FLAGS_fast_stdout) {
    log_lock.lock();
  }
#if defined(OutputDebugString)
  OutputDebugStringA(buffer);
#else
  fprintf(stderr, buffer);
  fflush(stderr);
#endif  // OutputDebugString
  if (!FLAGS_fast_stdout) {
    log_lock.unlock();
  }

#if XE_LIKE_WIN32
  if (!poly::has_console_attached()) {
    MessageBoxA(NULL, buffer, "Xenia Error",
                MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
  }
#endif  // WIN32

  exit(1);
}

}  // namespace poly
