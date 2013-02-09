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


void xe_log_line(const char* file_path, const uint32_t line_number,
                 const char* function_name, const char level_char,
                 const char* fmt, ...) {
  const int kLogMax = 2048;

  // Strip out just the filename from the path.
  const char* filename = xestrrchra(file_path, XE_PATH_SEPARATOR);
  if (filename) {
    // Slash - skip over it.
    filename++;
  } else {
    // No slash, entire thing is filename.
    filename = file_path;
  }

  // Scribble args into the print buffer.
  va_list args;
  va_start(args, fmt);
  char buffer[kLogMax];
  int buffer_length = xevsnprintfa(buffer, XECOUNT(buffer), fmt, args);
  va_end(args);
  if (buffer_length < 0) {
    return;
  }

  // Format string - add a trailing newline if required.
  const char* outfmt;
  if ((buffer_length >= 1) && buffer[buffer_length - 1] == '\n') {
    outfmt = "XE[%c] %s:%d: %s";
  } else {
    outfmt = "XE[%c] %s:%d: %s\n";
  }

#if defined(OutputDebugString)
  char full_output[kLogMax];
  if (xesnprintfa(full_output, XECOUNT(buffer), outfmt, level_char,
                  filename, line_number, buffer) >= 0) {
    OutputDebugStringA(full_output);
  }
#else
  XEIGNORE(fprintf(stdout, outfmt, level_char, filename, line_number, buffer));
#endif  // OutputDebugString
}
