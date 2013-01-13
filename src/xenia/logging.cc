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


void xe_log_line(const xechar_t* file_path, const uint32_t line_number,
                 const xechar_t* function_name, const xechar_t level_char,
                 const xechar_t* fmt, ...) {
  const int kLogMax = 2048;

  // Strip out just the filename from the path.
  const xechar_t* filename = xestrrchr(file_path, XE_PATH_SEPARATOR);
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
  xechar_t buffer[kLogMax];
  int buffer_length = xevsnprintf(buffer, XECOUNT(buffer), fmt, args);
  va_end(args);
  if (buffer_length < 0) {
    return;
  }

  // Format string - add a trailing newline if required.
  const xechar_t* outfmt;
  if ((buffer_length >= 1) && buffer[buffer_length - 1] == '\n') {
    outfmt = XT("XE[%c] %s:%d: %s");
  } else {
    outfmt = XT("XE[%c] %s:%d: %s\n");
  }

#if defined(OutputDebugString)
  xechar_t full_output[kLogMax];
  if (xesnprintf(full_output, XECOUNT(buffer), outfmt, level_char,
                 filename, line_number, buffer) >= 0) {
    OutputDebugString(full_output);
  }
#elif defined(XE_WCHAR)
  XEIGNORE(fwprintf(stdout, outfmt, level_char, filename, line_number, buffer));
#else
  XEIGNORE(fprintf(stdout, outfmt, level_char, filename, line_number, buffer));
#endif  // OutputDebugString
}
