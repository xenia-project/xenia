/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl_rtl.h>

#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xboxkrnl_private.h>
#include <xenia/kernel/objects/xthread.h>
#include <xenia/kernel/objects/xuser_module.h>
#include <xenia/kernel/util/shim_utils.h>
#include <xenia/kernel/util/xex2.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {


// TODO: clean me up!
SHIM_CALL vsprintf_shim(
    PPCContext* ppc_state, KernelState* state) {

  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  uint32_t format_ptr = SHIM_GET_ARG_32(1);
  uint32_t arg_ptr = SHIM_GET_ARG_32(2);

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
    while (*end == '-' ||
           *end == '+' ||
           *end == ' ' ||
           *end == '#' ||
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
    }
    else {
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
      }
      else {
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
    }
    else if (*end == 'l') {
      ++end;
      arg_size = 4;
      if (*end == 'l') {
        ++end;
        arg_size = 8;
      }
    }
    else if (*end == 'j') {
      arg_size = 8;
      ++end;
    }
    else if (*end == 'z') {
      arg_size = 4;
      ++end;
    }
    else if (*end == 't') {
      arg_size = 8;
      ++end;
    }
    else if (*end == 'L') {
      arg_size = 8;
      ++end;
    }

    if (*end == '\0') {
      break;
    }

    if (*end == 'd' ||
        *end == 'i' ||
        *end == 'u' ||
        *end == 'o' ||
        *end == 'x' ||
        *end == 'X' ||
        *end == 'f' ||
        *end == 'F' ||
        *end == 'e' ||
        *end == 'E' ||
        *end == 'g' ||
        *end == 'G' ||
        *end == 'a' ||
        *end == 'A' ||
        *end == 'c') {
      char local[512];
      local[0] = '\0';
      strncat(local, start, end + 1 - start);

      assert_true(arg_size == 8 || arg_size == 4);
      if (arg_size == 8) {
        if (arg_extras == 0) {
          uint64_t value = SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
          int result = sprintf(b, local, value);
          b += result;
          arg_index++;
        }
        else {
          assert_true(false);
        }
      }
      else if (arg_size == 4) {
        if (arg_extras == 0) {
          uint32_t value = (uint32_t)SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
          int result = sprintf(b, local, value);
          b += result;
          arg_index++;
        }
        else {
          assert_true(false);
        }
      }
    }
    else if (*end == 'n')
    {
      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
        SHIM_SET_MEM_32(value,  (uint32_t)((b - buffer) / sizeof(char)));
        arg_index++;
      }
      else {
        assert_true(false);
      }
    }
    else if (*end == 's' ||
             *end == 'p') {
      char local[512];
      local[0] = '\0';
      strncat(local, start, end + 1 - start);

      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
        const void* pointer = (const void*)SHIM_MEM_ADDR(value);
        int result = sprintf(b, local, pointer);
        b += result;
        arg_index++;
      }
      else {
        assert_true(false);
      }
    }
    else {
      assert_true(false);
      break;
    }
    format = end;
  }
  *b = '\0';
  SHIM_SET_RETURN_32((uint32_t)(b - buffer));
}


// TODO: clean me up!
SHIM_CALL _vsnprintf_shim(
    PPCContext* ppc_state, KernelState* state) {

  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  uint32_t count = SHIM_GET_ARG_32(1);
  uint32_t format_ptr = SHIM_GET_ARG_32(2);
  uint32_t arg_ptr = SHIM_GET_ARG_32(3);

  if (format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  char* buffer = (char*)SHIM_MEM_ADDR(buffer_ptr);  // TODO: ensure it never writes past the end of the buffer (count)...
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
    while (*end == '-' ||
           *end == '+' ||
           *end == ' ' ||
           *end == '#' ||
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
    }
    else {
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
      }
      else {
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
    }
    else if (*end == 'l') {
      ++end;
      arg_size = 4;
      if (*end == 'l') {
        ++end;
        arg_size = 8;
      }
    }
    else if (*end == 'j') {
      arg_size = 8;
      ++end;
    }
    else if (*end == 'z') {
      arg_size = 4;
      ++end;
    }
    else if (*end == 't') {
      arg_size = 8;
      ++end;
    }
    else if (*end == 'L') {
      arg_size = 8;
      ++end;
    }

    if (*end == '\0') {
      break;
    }

    if (*end == 'd' ||
        *end == 'i' ||
        *end == 'u' ||
        *end == 'o' ||
        *end == 'x' ||
        *end == 'X' ||
        *end == 'f' ||
        *end == 'F' ||
        *end == 'e' ||
        *end == 'E' ||
        *end == 'g' ||
        *end == 'G' ||
        *end == 'a' ||
        *end == 'A' ||
        *end == 'c') {
      char local[512];
      local[0] = '\0';
      strncat(local, start, end + 1 - start);

      assert_true(arg_size == 8 || arg_size == 4);
      if (arg_size == 8) {
        if (arg_extras == 0) {
          uint64_t value = SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
          int result = sprintf(b, local, value);
          b += result;
          arg_index++;
        }
        else {
          assert_true(false);
        }
      }
      else if (arg_size == 4) {
        if (arg_extras == 0) {
          uint32_t value = (uint32_t)SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
          int result = sprintf(b, local, value);
          b += result;
          arg_index++;
        }
        else {
          assert_true(false);
        }
      }
    }
    else if (*end == 'n')
    {
      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
        SHIM_SET_MEM_32(value,  (uint32_t)((b - buffer) / sizeof(char)));
        arg_index++;
      }
      else {
        assert_true(false);
      }
    }
    else if (*end == 's' ||
             *end == 'p') {
      char local[512];
      local[0] = '\0';
      strncat(local, start, end + 1 - start);

      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
        const void* pointer = (const void*)SHIM_MEM_ADDR(value);
        int result = sprintf(b, local, pointer);
        b += result;
        arg_index++;
      }
      else {
        assert_true(false);
      }
    }
    else {
      assert_true(false);
      break;
    }
    format = end;
  }
  *b = '\0';
  SHIM_SET_RETURN_32((uint32_t)(b - buffer));
}


// TODO: clean me up!
SHIM_CALL _vswprintf_shim(
    PPCContext* ppc_state, KernelState* state) {

  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  uint32_t format_ptr = SHIM_GET_ARG_32(1);
  uint32_t arg_ptr = SHIM_GET_ARG_32(2);

  if (format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  wchar_t*buffer = (wchar_t*)SHIM_MEM_ADDR(buffer_ptr);  // TODO: ensure it never writes past the end of the buffer (count)...
  const wchar_t* format = (const wchar_t*)SHIM_MEM_ADDR(format_ptr);

  // this will work since a null is the same regardless of endianness
  size_t format_length = wcslen(format);

  // swap the format buffer
  wchar_t* swapped_format = (wchar_t*)xe_malloc((format_length + 1) * sizeof(wchar_t));
  for (size_t i = 0; i < format_length; ++i) {
    swapped_format[i] = poly::byte_swap(format[i]);
  }
  swapped_format[format_length] = '\0';

  // be sneaky
  format = swapped_format;

  int arg_index = 0;

  wchar_t* b = buffer;
  for (; *format != '\0'; ++format) {
    const wchar_t *start = format;

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
    while (*end == '-' ||
           *end == '+' ||
           *end == ' ' ||
           *end == '#' ||
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
    }
    else {
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
      }
      else {
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
    }
    else if (*end == 'l') {
      ++end;
      arg_size = 4;
      if (*end == 'l') {
        ++end;
        arg_size = 8;
      }
    }
    else if (*end == 'j') {
      arg_size = 8;
      ++end;
    }
    else if (*end == 'z') {
      arg_size = 4;
      ++end;
    }
    else if (*end == 't') {
      arg_size = 8;
      ++end;
    }
    else if (*end == 'L') {
      arg_size = 8;
      ++end;
    }

    if (*end == '\0') {
      break;
    }

    if (*end == 'd' ||
        *end == 'i' ||
        *end == 'u' ||
        *end == 'o' ||
        *end == 'x' ||
        *end == 'X' ||
        *end == 'f' ||
        *end == 'F' ||
        *end == 'e' ||
        *end == 'E' ||
        *end == 'g' ||
        *end == 'G' ||
        *end == 'a' ||
        *end == 'A' ||
        *end == 'c') {
      wchar_t local[512];
      local[0] = '\0';
      wcsncat(local, start, end + 1 - start);

      assert_true(arg_size == 8 || arg_size == 4);
      if (arg_size == 8) {
        if (arg_extras == 0) {
          uint64_t value = SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
          int result = wsprintf(b, local, value);
          b += result;
          arg_index++;
        }
        else {
          assert_true(false);
        }
      }
      else if (arg_size == 4) {
        if (arg_extras == 0) {
          uint32_t value = (uint32_t)SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
          int result = wsprintf(b, local, value);
          b += result;
          arg_index++;
        }
        else {
          assert_true(false);
        }
      }
    }
    else if (*end == 'n')
    {
      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
        SHIM_SET_MEM_32(value,  (uint32_t)((b - buffer) / sizeof(wchar_t)));
        arg_index++;
      }
      else {
        assert_true(false);
      }
    }
    else if (*end == 'p') {
      wchar_t local[512];
      local[0] = '\0';
      wcsncat(local, start, end + 1 - start);

      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
        const void* pointer = (void*)SHIM_MEM_ADDR(value);
        int result = wsprintf(b, local, pointer);
        b += result;
        arg_index++;
      }
      else {
        assert_true(false);
      }
    }
    else if (*end == 's') {
      wchar_t local[512];
      local[0] = '\0';
      wcsncat(local, start, end + 1 - start);

      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
        const wchar_t* data = (const wchar_t*)SHIM_MEM_ADDR(value);
        size_t data_length = wcslen(data);
        wchar_t* swapped_data = (wchar_t*)xe_malloc((data_length + 1) * sizeof(wchar_t));
        for (size_t i = 0; i < data_length; ++i) {
          swapped_data[i] = poly::byte_swap(data[i]);
        }
        swapped_data[data_length] = '\0';
        int result = wsprintf(b, local, swapped_data);
        xe_free(swapped_data);
        b += result;
        arg_index++;
      }
      else {
        assert_true(false);
      }
    }
    else {
      assert_true(false);
      break;
    }
    format = end;
  }
  *b = '\0';

  xe_free(swapped_format);

  // swap the result buffer
  for (wchar_t* swap = buffer; swap != b; ++swap)
  {
    *swap = poly::byte_swap(*swap);
  }

  SHIM_SET_RETURN_32((uint32_t)((b - buffer) / sizeof(wchar_t)));
}


}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterStringExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", vsprintf, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", _vsnprintf, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", _vswprintf, state);
}
