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
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

// TODO: clean me up!
SHIM_CALL DbgPrint_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t format_ptr = SHIM_GET_ARG_32(0);
  if (format_ptr == 0) {
    SHIM_SET_RETURN_64(-1);
    return;
  }

  const char* format = (const char*)SHIM_MEM_ADDR(format_ptr);

  int arg_index = 0;

  char buffer[512];  // TODO: ensure it never writes past the end of the
                     // buffer...
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
          uint64_t value =
              arg_index < 7
                  ? SHIM_GET_ARG_64(1 + arg_index)
                  : SHIM_MEM_32(SHIM_GPR_32(1) + 16 + ((1 + arg_index) * 8));
          int result = sprintf(b, local, value);
          b += result;
          arg_index++;
        } else {
          assert_true(false);
        }
      } else if (arg_size == 4) {
        if (arg_extras == 0) {
          uint64_t value =
              arg_index < 7
                  ? SHIM_GET_ARG_64(1 + arg_index)
                  : SHIM_MEM_32(SHIM_GPR_32(1) + 16 + ((1 + arg_index) * 8));
          int result = sprintf(b, local, (uint32_t)value);
          b += result;
          arg_index++;
        } else {
          assert_true(false);
        }
      }
    } else if (*end == 'n') {
      assert_true(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = arg_index < 7
                             ? SHIM_GET_ARG_32(1 + arg_index)
                             : (uint32_t)SHIM_MEM_64(SHIM_GPR_32(1) + 16 +
                                                     ((1 + arg_index) * 8));
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
        uint32_t value = arg_index < 7
                             ? SHIM_GET_ARG_32(1 + arg_index)
                             : (uint32_t)SHIM_MEM_64(SHIM_GPR_32(1) + 16 +
                                                     ((1 + arg_index) * 8));
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
  *b++ = '\0';

  XELOGD("(DbgPrint) %s", buffer);
}

SHIM_CALL DbgBreakPoint_shim(PPCContext* ppc_state, KernelState* state) {
  XELOGD("DbgBreakPoint()");
  DebugBreak();
}

// https://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx
typedef struct {
    xe::be<uint32_t> type;
    xe::be<uint32_t> name_ptr;
    xe::be<uint32_t> thread_id;
    xe::be<uint32_t> flags;
} X_THREADNAME_INFO;
static_assert_size(X_THREADNAME_INFO, 0x10);

// https://msdn.microsoft.com/en-us/library/windows/desktop/aa363082.aspx
typedef struct {
  xe::be<uint32_t> exception_code;
  xe::be<uint32_t> exception_flags;
  xe::be<uint32_t> exception_record;
  xe::be<uint32_t> exception_address;
  xe::be<uint32_t> number_parameters;
  xe::be<uint32_t> exception_information[15];
} X_EXCEPTION_RECORD;
static_assert_size(X_EXCEPTION_RECORD, 0x50);

SHIM_CALL RtlRaiseException_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t record_ptr = SHIM_GET_ARG_32(0);

  auto record = reinterpret_cast<X_EXCEPTION_RECORD*>(SHIM_MEM_ADDR(record_ptr));

  XELOGD("RtlRaiseException(%.8X(%.8X))", record_ptr, record->exception_code);

  if (record->exception_code == 0x406D1388) {
    // SetThreadName. FFS.
    // https://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx

    // TODO: check record->number_parameters to make sure it's a correct size
    auto thread_info = reinterpret_cast<X_THREADNAME_INFO*>(&record->exception_information[0]);

    assert_true(thread_info->type == 0x1000);
    const char* name = (const char*)SHIM_MEM_ADDR(thread_info->name_ptr);

    object_ref<XThread> thread;
    if (thread_info->thread_id == -1) {
      // Current thread.
      thread = retain_object(XThread::GetCurrentThread());
    } else {
      // Lookup thread by ID.
      thread = state->GetThreadByID(thread_info->thread_id);
    }

    if (thread) {
      XELOGD("SetThreadName(%d, %s)", thread->thread_id(), name);
      thread->set_name(name);
    }

    // TODO(benvanik): unwinding required here?
    return;
  }

  if (record->exception_code == 0xE06D7363) {
    // C++ exception.
    // http://blogs.msdn.com/b/oldnewthing/archive/2010/07/30/10044061.aspx
    DebugBreak();
    return;
  }

  // TODO(benvanik): unwinding.
  // This is going to suck.
  DebugBreak();
}

void xeKeBugCheckEx(uint32_t code, uint32_t param1, uint32_t param2,
                    uint32_t param3, uint32_t param4) {
  XELOGD("*** STOP: 0x%.8X (0x%.8X, 0x%.8X, 0x%.8X, 0x%.8X)", code, param1,
         param2, param3, param4);
  fflush(stdout);
  DebugBreak();
  assert_always();
}

SHIM_CALL KeBugCheck_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t code = SHIM_GET_ARG_32(0);
  xeKeBugCheckEx(code, 0, 0, 0, 0);
}

SHIM_CALL KeBugCheckEx_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t code = SHIM_GET_ARG_32(0);
  uint32_t param1 = SHIM_GET_ARG_32(1);
  uint32_t param2 = SHIM_GET_ARG_32(2);
  uint32_t param3 = SHIM_GET_ARG_32(3);
  uint32_t param4 = SHIM_GET_ARG_32(4);
  xeKeBugCheckEx(code, param1, param2, param3, param4);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterDebugExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", DbgPrint, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", DbgBreakPoint, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlRaiseException, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeBugCheck, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeBugCheckEx, state);
}
