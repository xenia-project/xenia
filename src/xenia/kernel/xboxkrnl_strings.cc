/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/kernel/objects/xuser_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/util/xex2.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

SHIM_CALL sprintf_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  uint32_t format_ptr = SHIM_GET_ARG_32(1);

  XELOGD("sprintf(...)");

  if (format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  char* buffer = (char*)SHIM_MEM_ADDR(buffer_ptr);
  const char* format = (const char*)SHIM_MEM_ADDR(format_ptr);

  int arg_index = 2;
  uint32_t sp = (uint32_t)ppc_state->r[1];
#define LOAD_SPRINTF_ARG(ARG) \
  (ARG < 8) ? SHIM_GET_ARG_32(ARG) : SHIM_MEM_64(sp + 0x54 + 8)

  char* b = buffer;
  for (; *format != '\0'; ++format) {
    const char* start = format;

    if (*format != '%') {
      *b++ = *format;
      continue;
    }

    ++format;
    if (*format == '\0') {
      break;
    }

    if (*format == '%') {
      *b++ = *format;
      continue;
    }

    const char* end;
    end = format;

    // skip flags
    while (*end == '-' || *end == '+' || *end == ' ' || *end == '#' ||
           *end == '0') {
      ++end;
    }

    if (*end == '\0') {
      break;
    }

    int arg_extras = 0;

    // skip width
    if (*end == '*') {
      ++end;
      arg_extras++;
    } else {
      while (*end >= '0' && *end <= '9') {
        ++end;
      }
    }

    if (*end == '\0') {
      break;
    }

    // skip precision
    if (*end == '.') {
      ++end;

      if (*end == '*') {
        ++end;
        ++arg_extras;
      } else {
        while (*end >= '0' && *end <= '9') {
          ++end;
        }
      }
    }

    if (*end == '\0') {
      break;
    }

    // get length
    int arg_size = 4;

    if (*end == 'h') {
      ++end;
      arg_size = 4;
      if (*end == 'h') {
        ++end;
      }
    } else if (*end == 'l') {
      ++end;
      arg_size = 4;
      if (*end == 'l') {
        ++end;
        arg_size = 8;
      }
    } else if (*end == 'j') {
      arg_size = 8;
      ++end;
    } else if (*end == 'z') {
      arg_size = 4;
      ++end;
    } else if (*end == 't') {
      arg_size = 8;
      ++end;
    } else if (*end == 'L') {
      arg_size = 8;
      ++end;
    }

    if (*end == '\0') {
      break;
    }

    if (*end == 'd' || *end == 'i' || *end == 'u' || *end == 'o' ||
        *end == 'x' || *end == 'X' || *end == 'f' || *end == 'F' ||
        *end == 'e' || *end == 'E' || *end == 'g' || *end == 'G' ||
        *end == 'a' || *end == 'A' || *end == 'c') {
      char local[512];
      local[0] = '\0';
      strncat(local, start, end + 1 - start);

      assert_true(arg_size == 8 || arg_size == 4);
      if (arg_size == 8) {
        if (arg_extras == 0) {
          uint64_t value = LOAD_SPRINTF_ARG(arg_index);
          int result = sprintf(b, local, value);
          b += result;
          arg_index++;
        } else {
          assert_true(false);
        }
      } else if (arg_size == 4) {
        if (arg_extras == 0) {
          uint32_t value = uint32_t(LOAD_SPRINTF_ARG(arg_index));
          int result = sprintf(b, local, value);
          b += result;
          arg_index++;
        } else {
          assert_true(false);
        }
      }
    } else if (*end == 'n') {
      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = uint32_t(LOAD_SPRINTF_ARG(arg_index));
        SHIM_SET_MEM_32(value, (uint32_t)((b - buffer) / sizeof(char)));
        arg_index++;
      } else {
        assert_true(false);
      }
    } else if (*end == 's' || *end == 'p') {
      char local[512];
      local[0] = '\0';
      strncat(local, start, end + 1 - start);

      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = uint32_t(LOAD_SPRINTF_ARG(arg_index));
        const void* pointer = (const void*)SHIM_MEM_ADDR(value);
        int result = sprintf(b, local, pointer);
        b += result;
        arg_index++;
      } else {
        assert_true(false);
      }
    } else {
      assert_true(false);
      break;
    }
    format = end;
  }
  *b = '\0';
  SHIM_SET_RETURN_32((uint32_t)(b - buffer));
}

// TODO: clean me up!
SHIM_CALL vsprintf_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  uint32_t format_ptr = SHIM_GET_ARG_32(1);
  uint32_t arg_ptr = SHIM_GET_ARG_32(2);

  XELOGD("_vsprintf(...)");

  if (format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  char* buffer = (char*)SHIM_MEM_ADDR(buffer_ptr);
  const char* format = (const char*)SHIM_MEM_ADDR(format_ptr);

  int arg_index = 0;

  char* b = buffer;
  for (; *format != '\0'; ++format) {
    const char* start = format;

    if (*format != '%') {
      *b++ = *format;
      continue;
    }

    ++format;
    if (*format == '\0') {
      break;
    }

    if (*format == '%') {
      *b++ = *format;
      continue;
    }

    const char* end;
    end = format;

    // skip flags
    while (*end == '-' || *end == '+' || *end == ' ' || *end == '#' ||
           *end == '0') {
      ++end;
    }

    if (*end == '\0') {
      break;
    }

    int arg_extras = 0;

    // skip width
    if (*end == '*') {
      ++end;
      arg_extras++;
    } else {
      while (*end >= '0' && *end <= '9') {
        ++end;
      }
    }

    if (*end == '\0') {
      break;
    }

    // skip precision
    if (*end == '.') {
      ++end;

      if (*end == '*') {
        ++end;
        ++arg_extras;
      } else {
        while (*end >= '0' && *end <= '9') {
          ++end;
        }
      }
    }

    if (*end == '\0') {
      break;
    }

    // get length
    int arg_size = 4;

    if (*end == 'h') {
      ++end;
      arg_size = 4;
      if (*end == 'h') {
        ++end;
      }
    } else if (*end == 'l') {
      ++end;
      arg_size = 4;
      if (*end == 'l') {
        ++end;
        arg_size = 8;
      }
    } else if (*end == 'j') {
      arg_size = 8;
      ++end;
    } else if (*end == 'z') {
      arg_size = 4;
      ++end;
    } else if (*end == 't') {
      arg_size = 8;
      ++end;
    } else if (*end == 'L') {
      arg_size = 8;
      ++end;
    }

    if (*end == '\0') {
      break;
    }

    if (*end == 'd' || *end == 'i' || *end == 'u' || *end == 'o' ||
        *end == 'x' || *end == 'X' || *end == 'f' || *end == 'F' ||
        *end == 'e' || *end == 'E' || *end == 'g' || *end == 'G' ||
        *end == 'a' || *end == 'A' || *end == 'c') {
      char local[512];
      local[0] = '\0';
      strncat(local, start, end + 1 - start);

      assert_true(arg_size == 8 || arg_size == 4);
      if (arg_size == 8) {
        if (arg_extras == 0) {
          uint64_t value = SHIM_MEM_64(
              arg_ptr + (arg_index * 8));  // TODO: check if this is correct...
          int result = sprintf(b, local, value);
          b += result;
          arg_index++;
        } else {
          assert_true(false);
        }
      } else if (arg_size == 4) {
        if (arg_extras == 0) {
          uint32_t value = (uint32_t)SHIM_MEM_64(
              arg_ptr + (arg_index * 8));  // TODO: check if this is correct...
          int result = sprintf(b, local, value);
          b += result;
          arg_index++;
        } else {
          assert_true(false);
        }
      }
    } else if (*end == 'n') {
      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)SHIM_MEM_64(
            arg_ptr + (arg_index * 8));  // TODO: check if this is correct...
        SHIM_SET_MEM_32(value, (uint32_t)((b - buffer) / sizeof(char)));
        arg_index++;
      } else {
        assert_true(false);
      }
    } else if (*end == 's' || *end == 'p') {
      char local[512];
      local[0] = '\0';
      strncat(local, start, end + 1 - start);

      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)SHIM_MEM_64(
            arg_ptr + (arg_index * 8));  // TODO: check if this is correct...
        const void* pointer = (const void*)SHIM_MEM_ADDR(value);
        int result = sprintf(b, local, pointer);
        b += result;
        arg_index++;
      } else {
        assert_true(false);
      }
    } else if (*end == 'S') {
      char local[512];
      local[0] = '\0';
      strncat(local, start, end + 1 - start);

      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)SHIM_MEM_64(
            arg_ptr + (arg_index * 8));  // TODO: check if this is correct...
        const wchar_t* data = (const wchar_t*)SHIM_MEM_ADDR(value);
        size_t data_length = wcslen(data);
        wchar_t* swapped_data =
            (wchar_t*)malloc((data_length + 1) * sizeof(wchar_t));
        for (size_t i = 0; i < data_length; ++i) {
          swapped_data[i] = xe::byte_swap(data[i]);
        }
        swapped_data[data_length] = '\0';
        int result = sprintf(b, local, swapped_data);
        free(swapped_data);
        b += result;
        arg_index++;
      } else {
        assert_true(false);
      }
    } else {
      assert_true(false);
      break;
    }
    format = end;
  }
  *b = '\0';
  SHIM_SET_RETURN_32((uint32_t)(b - buffer));
}

// TODO: clean me up!
SHIM_CALL _vsnprintf_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  uint32_t count = SHIM_GET_ARG_32(1);
  uint32_t format_ptr = SHIM_GET_ARG_32(2);
  uint32_t arg_ptr = SHIM_GET_ARG_32(3);

  XELOGD("_vsnprintf(...)");

  if (format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  char* buffer = (char*)SHIM_MEM_ADDR(buffer_ptr);  // TODO: ensure it never
                                                    // writes past the end of
                                                    // the buffer (count)...
  const char* format = (const char*)SHIM_MEM_ADDR(format_ptr);

  int arg_index = 0;

  char* b = buffer;
  for (; *format != '\0'; ++format) {
    const char* start = format;

    if (*format != '%') {
      *b++ = *format;
      continue;
    }

    ++format;
    if (*format == '\0') {
      break;
    }

    if (*format == '%') {
      *b++ = *format;
      continue;
    }

    const char* end;
    end = format;

    // skip flags
    while (*end == '-' || *end == '+' || *end == ' ' || *end == '#' ||
           *end == '0') {
      ++end;
    }

    if (*end == '\0') {
      break;
    }

    int arg_extras = 0;

    // skip width
    if (*end == '*') {
      ++end;
      arg_extras++;
    } else {
      while (*end >= '0' && *end <= '9') {
        ++end;
      }
    }

    if (*end == '\0') {
      break;
    }

    // skip precision
    if (*end == '.') {
      ++end;

      if (*end == '*') {
        ++end;
        ++arg_extras;
      } else {
        while (*end >= '0' && *end <= '9') {
          ++end;
        }
      }
    }

    if (*end == '\0') {
      break;
    }

    // get length
    int arg_size = 4;

    if (*end == 'h') {
      ++end;
      arg_size = 4;
      if (*end == 'h') {
        ++end;
      }
    } else if (*end == 'l') {
      ++end;
      arg_size = 4;
      if (*end == 'l') {
        ++end;
        arg_size = 8;
      }
    } else if (*end == 'j') {
      arg_size = 8;
      ++end;
    } else if (*end == 'z') {
      arg_size = 4;
      ++end;
    } else if (*end == 't') {
      arg_size = 8;
      ++end;
    } else if (*end == 'L') {
      arg_size = 8;
      ++end;
    }

    if (*end == '\0') {
      break;
    }

    if (*end == 'd' || *end == 'i' || *end == 'u' || *end == 'o' ||
        *end == 'x' || *end == 'X' || *end == 'f' || *end == 'F' ||
        *end == 'e' || *end == 'E' || *end == 'g' || *end == 'G' ||
        *end == 'a' || *end == 'A' || *end == 'c') {
      char local[512];
      local[0] = '\0';
      strncat(local, start, end + 1 - start);

      assert_true(arg_size == 8 || arg_size == 4);
      if (arg_size == 8) {
        if (arg_extras == 0) {
          uint64_t value = SHIM_MEM_64(
              arg_ptr + (arg_index * 8));  // TODO: check if this is correct...
          int result = sprintf(b, local, value);
          b += result;
          arg_index++;
        } else {
          assert_true(false);
        }
      } else if (arg_size == 4) {
        if (arg_extras == 0) {
          uint32_t value = (uint32_t)SHIM_MEM_64(
              arg_ptr + (arg_index * 8));  // TODO: check if this is correct...
          int result = sprintf(b, local, value);
          b += result;
          arg_index++;
        } else {
          assert_true(false);
        }
      }
    } else if (*end == 'n') {
      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)SHIM_MEM_64(
            arg_ptr + (arg_index * 8));  // TODO: check if this is correct...
        SHIM_SET_MEM_32(value, (uint32_t)(b - buffer));
        arg_index++;
      } else {
        assert_true(false);
      }
    } else if (*end == 's' || *end == 'p') {
      char local[512];
      local[0] = '\0';
      strncat(local, start, end + 1 - start);

      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)SHIM_MEM_64(
            arg_ptr + (arg_index * 8));  // TODO: check if this is correct...
        const void* pointer = (const void*)SHIM_MEM_ADDR(value);
        int result = sprintf(b, local, pointer);
        b += result;
        arg_index++;
      } else {
        assert_true(false);
      }
    } else {
      assert_true(false);
      break;
    }
    format = end;
  }
  *b = '\0';
  SHIM_SET_RETURN_32((uint32_t)(b - buffer));
}

uint32_t vswprintf_core(wchar_t* buffer, const wchar_t* format,
                        const uint8_t* arg_ptr, uint8_t* membase) {
  // this will work since a null is the same regardless of endianness
  size_t format_length = wcslen(format);

  // swap the format buffer
  wchar_t* swapped_format =
      (wchar_t*)malloc((format_length + 1) * sizeof(wchar_t));
  for (size_t i = 0; i < format_length; ++i) {
    swapped_format[i] = xe::byte_swap(format[i]);
  }
  swapped_format[format_length] = '\0';

  // be sneaky
  format = swapped_format;

  int arg_index = 0;

  wchar_t* b = buffer;
  for (; *format != '\0'; ++format) {
    const wchar_t* start = format;

    if (*format != '%') {
      *b++ = *format;
      continue;
    }

    ++format;
    if (*format == '\0') {
      break;
    }

    if (*format == '%') {
      *b++ = *format;
      continue;
    }

    const wchar_t* end;
    end = format;

    // skip flags
    while (*end == '-' || *end == '+' || *end == ' ' || *end == '#' ||
           *end == '0') {
      ++end;
    }

    if (*end == '\0') {
      break;
    }

    int arg_extras = 0;

    // skip width
    if (*end == '*') {
      ++end;
      arg_extras++;
    } else {
      while (*end >= '0' && *end <= '9') {
        ++end;
      }
    }

    if (*end == '\0') {
      break;
    }

    // skip precision
    if (*end == '.') {
      ++end;

      if (*end == '*') {
        ++end;
        ++arg_extras;
      } else {
        while (*end >= '0' && *end <= '9') {
          ++end;
        }
      }
    }

    if (*end == '\0') {
      break;
    }

    // get length
    int arg_size = 4;

    if (*end == 'h') {
      ++end;
      arg_size = 4;
      if (*end == 'h') {
        ++end;
      }
    } else if (*end == 'l') {
      ++end;
      arg_size = 4;
      if (*end == 'l') {
        ++end;
        arg_size = 8;
      }
    } else if (*end == 'j') {
      arg_size = 8;
      ++end;
    } else if (*end == 'z') {
      arg_size = 4;
      ++end;
    } else if (*end == 't') {
      arg_size = 8;
      ++end;
    } else if (*end == 'L') {
      arg_size = 8;
      ++end;
    }

    if (*end == '\0') {
      break;
    }

    if (*end == 'd' || *end == 'i' || *end == 'u' || *end == 'o' ||
        *end == 'x' || *end == 'X' || *end == 'f' || *end == 'F' ||
        *end == 'e' || *end == 'E' || *end == 'g' || *end == 'G' ||
        *end == 'a' || *end == 'A' || *end == 'c') {
      wchar_t local[512];
      local[0] = '\0';
      wcsncat(local, start, end + 1 - start);

      assert_true(arg_size == 8 || arg_size == 4);
      if (arg_size == 8) {
        if (arg_extras == 0) {
          uint64_t value = xe::load_and_swap<uint64_t>(
              arg_ptr + (arg_index * 8));  // TODO: check if this is correct...
          int result = wsprintf(b, local, value);
          b += result;
          arg_index++;
        } else {
          assert_true(false);
        }
      } else if (arg_size == 4) {
        if (arg_extras == 0) {
          uint32_t value = (uint32_t)xe::load_and_swap<uint64_t>(
              arg_ptr + (arg_index * 8));  // TODO: check if this is correct...
          int result = wsprintf(b, local, value);
          b += result;
          arg_index++;
        } else {
          assert_true(false);
        }
      }
    } else if (*end == 'n') {
      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)xe::load_and_swap<uint64_t>(
            arg_ptr + (arg_index * 8));  // TODO: check if this is correct...
        xe::store_and_swap<uint32_t>(membase + value, (uint32_t)(b - buffer));
        arg_index++;
      } else {
        assert_true(false);
      }
    } else if (*end == 'p') {
      wchar_t local[512];
      local[0] = '\0';
      wcsncat(local, start, end + 1 - start);

      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)xe::load_and_swap<uint64_t>(
            arg_ptr + (arg_index * 8));  // TODO: check if this is correct...
        const void* pointer = (void*)(membase + value);
        int result = wsprintf(b, local, pointer);
        b += result;
        arg_index++;
      } else {
        assert_true(false);
      }
    } else if (*end == 's') {
      wchar_t local[512];
      local[0] = '\0';
      wcsncat(local, start, end + 1 - start);

      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)xe::load_and_swap<uint64_t>(
            arg_ptr + (arg_index * 8));  // TODO: check if this is correct...
        const wchar_t* data = (const wchar_t*)(membase + value);
        size_t data_length = wcslen(data);
        wchar_t* swapped_data =
            (wchar_t*)malloc((data_length + 1) * sizeof(wchar_t));
        for (size_t i = 0; i < data_length; ++i) {
          swapped_data[i] = xe::byte_swap(data[i]);
        }
        swapped_data[data_length] = '\0';
        int result = wsprintf(b, local, swapped_data);
        free(swapped_data);
        b += result;
        arg_index++;
      } else {
        assert_true(false);
      }
    } else {
      assert_true(false);
      break;
    }
    format = end;
  }
  *b = '\0';

  free(swapped_format);

  // swap the result buffer
  for (wchar_t* swap = buffer; swap != b; ++swap) {
    *swap = xe::byte_swap(*swap);
  }

  return uint32_t(b - buffer);
}

// TODO: clean me up!
SHIM_CALL _vswprintf_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  uint32_t format_ptr = SHIM_GET_ARG_32(1);
  uint32_t arg_ptr = SHIM_GET_ARG_32(2);

  XELOGD("_vswprintf(...)");

  if (format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }
  const wchar_t* format = (const wchar_t*)SHIM_MEM_ADDR(format_ptr);

  wchar_t* buffer =
      (wchar_t*)SHIM_MEM_ADDR(buffer_ptr);  // TODO: ensure it never writes past
                                            // the end of the buffer (count)...

  uint32_t ret =
      vswprintf_core(buffer, format, SHIM_MEM_ADDR(arg_ptr), SHIM_MEM_BASE);
  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL _vscwprintf_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t format_ptr = SHIM_GET_ARG_32(0);
  uint32_t arg_ptr = SHIM_GET_ARG_32(1);

  XELOGD("_vscwprintf(...)");

  if (format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }
  const wchar_t* format = (const wchar_t*)SHIM_MEM_ADDR(format_ptr);

  // HACK: this is the worst.
  auto temp = new wchar_t[2048];
  uint32_t ret =
      vswprintf_core(temp, format, SHIM_MEM_ADDR(arg_ptr), SHIM_MEM_BASE);
  delete[] temp;
  SHIM_SET_RETURN_32(ret);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterStringExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", sprintf, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", vsprintf, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", _vsnprintf, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", _vswprintf, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", _vscwprintf, state);
}
