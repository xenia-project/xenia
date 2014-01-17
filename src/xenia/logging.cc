/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/logging.h>

#include <xenia/common.h>


void xe_format_log_line(
    char* buffer, size_t buffer_count,
    const char* file_path, const uint32_t line_number,
    const char* function_name, const char level_char,
    const char* fmt, va_list args) {
  // Strip out just the filename from the path.
  const char* filename = xestrrchra(file_path, XE_PATH_SEPARATOR);
  if (filename) {
    // Slash - skip over it.
    filename++;
  } else {
    // No slash, entire thing is filename.
    filename = file_path;
  }

  // Format string - add a trailing newline if required.
  const char* outfmt = "XE[%c] %s:%d: ";
  char* buffer_ptr = buffer + xesnprintfa(
      buffer, buffer_count - 1, outfmt, level_char, filename, line_number);

  // Scribble args into the print buffer.
  buffer_ptr = buffer_ptr + xevsnprintfa(
      buffer_ptr, buffer_count - (buffer_ptr - buffer) - 1, fmt, args);

  // Add a trailing newline.
  if (buffer_ptr[-1] != '\n') {
    buffer_ptr[0] = '\n';
    buffer_ptr[1] = 0;
  }
}

void xe_log_line(const char* file_path, const uint32_t line_number,
                 const char* function_name, const char level_char,
                 const char* fmt, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, fmt);
  xe_format_log_line(buffer, XECOUNT(buffer),
                     file_path, line_number, function_name, level_char,
                     fmt, args);
  va_end(args);

  fprintf(stderr, buffer);
  fflush(stderr);

#if 0// defined(OutputDebugString)
  OutputDebugStringA(buffer);
#else
  XEIGNORE(fprintf(stdout, buffer));
  fflush(stdout);
#endif  // OutputDebugString
}

void xe_handle_fatal(
    const char* file_path, const uint32_t line_number,
    const char* function_name, const char* fmt, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, fmt);
  xe_format_log_line(buffer, XECOUNT(buffer),
                     file_path, line_number, function_name, 'X',
                     fmt, args);
  va_end(args);

#if defined(OutputDebugString)
  OutputDebugStringA(buffer);
#endif  // OutputDebugString

  fprintf(stderr, buffer);
  fflush(stderr);

#if XE_LIKE_WIN32
  if (!xe_has_console()) {
    MessageBoxA(NULL, buffer, "Xenia Error",
                MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
  }
#endif  // WIN32

  exit(1);
}
