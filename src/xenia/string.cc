/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/string.h>

#include <xenia/common.h>


#if XE_PLATFORM_WIN32

char* xestrcasestra(const char* str, const char* substr) {
  const size_t len = xestrlena(substr);
  while (*str) {
    if (!_strnicmp(str, substr, len)) {
      break;
    }
    str++;
  }

  if (*str) {
    return (char*)str;
  } else {
    return NULL;
  }
}

#endif  // WIN32
