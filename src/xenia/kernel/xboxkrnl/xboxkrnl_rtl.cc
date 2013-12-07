/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/xboxkrnl_rtl.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/xex2.h>
#include <xenia/kernel/xboxkrnl/kernel_state.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_private.h>
#include <xenia/kernel/xboxkrnl/objects/xmodule.h>
#include <xenia/kernel/xboxkrnl/objects/xthread.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {
namespace xboxkrnl {


// http://msdn.microsoft.com/en-us/library/ff561778
uint32_t xeRtlCompareMemory(uint32_t source1_ptr, uint32_t source2_ptr,
                            uint32_t length) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // SIZE_T
  // _In_  const VOID *Source1,
  // _In_  const VOID *Source2,
  // _In_  SIZE_T Length

  uint8_t* p1 = IMPL_MEM_ADDR(source1_ptr);
  uint8_t* p2 = IMPL_MEM_ADDR(source2_ptr);

  // Note that the return value is the number of bytes that match, so it's best
  // we just do this ourselves vs. using memcmp.
  // On Windows we could use the builtin function.

  uint32_t c = 0;
  for (uint32_t n = 0; n < length; n++, p1++, p2++) {
    if (*p1 == *p2) {
      c++;
    }
  }

  return c;
}


SHIM_CALL RtlCompareMemory_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t source1 = SHIM_GET_ARG_32(0);
  uint32_t source2 = SHIM_GET_ARG_32(1);
  uint32_t length = SHIM_GET_ARG_32(2);

  XELOGD(
      "RtlCompareMemory(%.8X, %.8X, %d)",
      source1, source2, length);

  uint32_t result = xeRtlCompareMemory(source1, source2, length);
  SHIM_SET_RETURN(result);
}


// http://msdn.microsoft.com/en-us/library/ff552123
uint32_t xeRtlCompareMemoryUlong(uint32_t source_ptr, uint32_t length,
                                 uint32_t pattern) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // SIZE_T
  // _In_  PVOID Source,
  // _In_  SIZE_T Length,
  // _In_  ULONG Pattern

  if ((source_ptr % 4) || (length % 4)) {
    return 0;
  }

  uint8_t* p = IMPL_MEM_ADDR(source_ptr);

  // Swap pattern.
  // TODO(benvanik): ensure byte order of pattern is correct.
  // Since we are doing byte-by-byte comparison we may not want to swap.
  // GET_ARG swaps, so this is a swap back. Ugly.
  const uint32_t pb32 = XESWAP32BE(pattern);
  const uint8_t* pb = (uint8_t*)&pb32;

  uint32_t c = 0;
  for (uint32_t n = 0; n < length; n++, p++) {
    if (*p == pb[n % 4]) {
      c++;
    }
  }

  return c;
}


SHIM_CALL RtlCompareMemoryUlong_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t source = SHIM_GET_ARG_32(0);
  uint32_t length = SHIM_GET_ARG_32(1);
  uint32_t pattern = SHIM_GET_ARG_32(2);

  XELOGD(
      "RtlCompareMemoryUlong(%.8X, %d, %.8X)",
      source, length, pattern);

  uint32_t result = xeRtlCompareMemoryUlong(source, length, pattern);
  SHIM_SET_RETURN(result);
}


// http://msdn.microsoft.com/en-us/library/ff552263
void xeRtlFillMemoryUlong(uint32_t destination_ptr, uint32_t length,
                          uint32_t pattern) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // VOID
  // _Out_  PVOID Destination,
  // _In_   SIZE_T Length,
  // _In_   ULONG Pattern

  // NOTE: length must be % 4, so we can work on uint32s.
  uint32_t* p = (uint32_t*)IMPL_MEM_ADDR(destination_ptr);

  // TODO(benvanik): ensure byte order is correct - we're writing back the
  // swapped arg value.

  uint32_t count = length >> 2;
  uint32_t native_pattern = XESWAP32BE(pattern);

  // TODO: unroll loop?

  for (uint32_t n = 0; n < count; n++, p++) {
    *p = native_pattern;
  }
}


SHIM_CALL RtlFillMemoryUlong_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t destination = SHIM_GET_ARG_32(0);
  uint32_t length = SHIM_GET_ARG_32(1);
  uint32_t pattern = SHIM_GET_ARG_32(2);

  XELOGD(
      "RtlFillMemoryUlong(%.8X, %d, %.8X)",
      destination, length, pattern);

  xeRtlFillMemoryUlong(destination, length, pattern);
}


// typedef struct _STRING {
//   USHORT Length;
//   USHORT MaximumLength;
//   PCHAR  Buffer;
// } ANSI_STRING, *PANSI_STRING;


// http://msdn.microsoft.com/en-us/library/ff561918
void xeRtlInitAnsiString(uint32_t destination_ptr, uint32_t source_ptr) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // VOID
  // _Out_     PANSI_STRING DestinationString,
  // _In_opt_  PCSZ SourceString

  const char* source = source_ptr ? (char*)IMPL_MEM_ADDR(source_ptr) : NULL;

  if (source) {
    uint16_t length = (uint16_t)xestrlena(source);
    IMPL_SET_MEM_16(destination_ptr + 0, length);
    IMPL_SET_MEM_16(destination_ptr + 2, length + 1);
    IMPL_SET_MEM_32(destination_ptr + 4, source_ptr);
  } else {
    IMPL_SET_MEM_16(destination_ptr + 0, 0);
    IMPL_SET_MEM_16(destination_ptr + 2, 0);
    IMPL_SET_MEM_32(destination_ptr + 4, 0);
  }
}


SHIM_CALL RtlInitAnsiString_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t source_ptr = SHIM_GET_ARG_32(1);

  const char* source = source_ptr ? (char*)SHIM_MEM_ADDR(source_ptr) : NULL;
  XELOGD("RtlInitAnsiString(%.8X, %.8X = %s)",
         destination_ptr, source_ptr, source ? source : "<null>");

  xeRtlInitAnsiString(destination_ptr, source_ptr);
}


// http://msdn.microsoft.com/en-us/library/ff561899
void xeRtlFreeAnsiString(uint32_t string_ptr) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // VOID
  // _Inout_  PANSI_STRING AnsiString

  //uint32_t buffer = SHIM_MEM_32(string_ptr + 4);
  // TODO(benvanik): free the buffer
  XELOGE("RtlFreeAnsiString leaking buffer");

  IMPL_SET_MEM_16(string_ptr + 0, 0);
  IMPL_SET_MEM_16(string_ptr + 2, 0);
  IMPL_SET_MEM_32(string_ptr + 4, 0);
}


SHIM_CALL RtlFreeAnsiString_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t string_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlFreeAnsiString(%.8X)", string_ptr);

  xeRtlFreeAnsiString(string_ptr);
}


// typedef struct _UNICODE_STRING {
//   USHORT Length;
//   USHORT MaximumLength;
//   PWSTR  Buffer;
// } UNICODE_STRING, *PUNICODE_STRING;


// http://msdn.microsoft.com/en-us/library/ff561934
void xeRtlInitUnicodeString(uint32_t destination_ptr, uint32_t source_ptr) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // VOID
  // _Out_     PUNICODE_STRING DestinationString,
  // _In_opt_  PCWSTR SourceString

  const wchar_t* source =
      source_ptr ? (const wchar_t*)IMPL_MEM_ADDR(source_ptr) : NULL;

  if (source) {
    uint16_t length = (uint16_t)xestrlenw(source);
    IMPL_SET_MEM_16(destination_ptr + 0, length * 2);
    IMPL_SET_MEM_16(destination_ptr + 2, (length + 1) * 2);
    IMPL_SET_MEM_32(destination_ptr + 4, source_ptr);
  } else {
    IMPL_SET_MEM_16(destination_ptr + 0, 0);
    IMPL_SET_MEM_16(destination_ptr + 2, 0);
    IMPL_SET_MEM_32(destination_ptr + 4, 0);
  }
}


SHIM_CALL RtlInitUnicodeString_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t source_ptr = SHIM_GET_ARG_32(1);

  const wchar_t* source =
      source_ptr ? (const wchar_t*)SHIM_MEM_ADDR(source_ptr) : NULL;
  XELOGD("RtlInitUnicodeString(%.8X, %.8X = %ls)",
         destination_ptr, source_ptr, source ? source : L"<null>");

  xeRtlInitUnicodeString(destination_ptr, source_ptr);
}


// http://msdn.microsoft.com/en-us/library/ff561903
void xeRtlFreeUnicodeString(uint32_t string_ptr) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // VOID
  // _Inout_  PUNICODE_STRING UnicodeString

  //uint32_t buffer = IMPL_MEM_32(string_ptr + 4);
  // TODO(benvanik): free the buffer
  XELOGE("RtlFreeUnicodeString leaking buffer");

  IMPL_SET_MEM_16(string_ptr + 0, 0);
  IMPL_SET_MEM_16(string_ptr + 2, 0);
  IMPL_SET_MEM_32(string_ptr + 4, 0);
}


SHIM_CALL RtlFreeUnicodeString_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t string_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlFreeUnicodeString(%.8X)", string_ptr);

  xeRtlFreeUnicodeString(string_ptr);
}


// http://msdn.microsoft.com/en-us/library/ff562969
X_STATUS xeRtlUnicodeStringToAnsiString(
    uint32_t destination_ptr, uint32_t source_ptr, uint32_t alloc_dest) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // NTSTATUS
  // _Inout_  PANSI_STRING DestinationString,
  // _In_     PCUNICODE_STRING SourceString,
  // _In_     BOOLEAN AllocateDestinationString

  XELOGE("RtlUnicodeStringToAnsiString not yet implemented");
  XEASSERTALWAYS();

  if (alloc_dest) {
    // Allocate a new buffer to place the string into.
    //IMPL_SET_MEM_32(destination_ptr + 4, buffer_ptr);
  } else {
    // Reuse the buffer in the target.
    //uint32_t buffer_size = IMPL_MEM_16(destination_ptr + 2);
  }

  return X_STATUS_UNSUCCESSFUL;
}


SHIM_CALL RtlUnicodeStringToAnsiString_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t source_ptr = SHIM_GET_ARG_32(1);
  uint32_t alloc_dest = SHIM_GET_ARG_32(2);

  XELOGD("RtlUnicodeStringToAnsiString(%.8X, %.8X, %d)",
         destination_ptr, source_ptr, alloc_dest);

  X_STATUS result = xeRtlUnicodeStringToAnsiString(
      destination_ptr, source_ptr, alloc_dest);
  SHIM_SET_RETURN(result);
}


// TODO: clean me up!
SHIM_CALL _vsnprintf_shim(
    PPCContext* ppc_state, KernelState* state) {

  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  uint32_t count = SHIM_GET_ARG_32(1);
  uint32_t format_ptr = SHIM_GET_ARG_32(2);
  uint32_t arg_ptr = SHIM_GET_ARG_32(3);

  if (format_ptr == 0) {
    SHIM_SET_RETURN(-1);
    return;
  }

  char *buffer = (char *)SHIM_MEM_ADDR(buffer_ptr);  // TODO: ensure it never writes past the end of the buffer (count)...
  const char *format = (const char *)SHIM_MEM_ADDR(format_ptr);

  int arg_index = 0;

  char *b = buffer;
  for (; *format != '\0'; ++format) {
    const char *start = format;

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

    const char *end;
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

      XEASSERT(arg_size == 8 || arg_size == 4);
      if (arg_size == 8) {
        if (arg_extras == 0) {
          uint64_t value = SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
          int result = sprintf(b, local, value);
          b += result;
          arg_index++;
        }
        else {
          XEASSERT(false);
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
          XEASSERT(false);
        }
      }
    }
    else if (*end == 's' ||
             *end == 'p' ||
             *end == 'n') {
      char local[512];
      local[0] = '\0';
      strncat(local, start, end + 1 - start);

      XEASSERT(arg_size == 4);
      if (arg_extras == 0) {
        uint32_t value = (uint32_t)SHIM_MEM_64(arg_ptr + (arg_index * 8)); // TODO: check if this is correct...
        const char *pointer = (const char *)SHIM_MEM_ADDR(value);
        int result = sprintf(b, local, pointer);
        b += result;
        arg_index++;
      }
      else {
        XEASSERT(false);
      }
    }
    else {
      XEASSERT(false);
      break;
    }
    format = end;
  }
  *b++ = '\0';
  SHIM_SET_RETURN((uint32_t)(b - buffer));
}


uint32_t xeRtlNtStatusToDosError(X_STATUS status) {
  if (!status || (status & 0x20000000)) {
    // Success.
    return status;
  } else if ((status & 0xF0000000) == 0xD0000000) {
    // High bit doesn't matter.
    status &= ~0x10000000;
  }

  // TODO(benvanik): implement lookup table.
  XELOGE("RtlNtStatusToDosError lookup NOT IMPLEMENTED");

  return 317; // ERROR_MR_MID_NOT_FOUND
}


SHIM_CALL RtlNtStatusToDosError_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t status = SHIM_GET_ARG_32(0);

  XELOGD(
      "RtlNtStatusToDosError(%.4X)",
      status);

  uint32_t result = xeRtlNtStatusToDosError(status);
  SHIM_SET_RETURN(result);
}


uint32_t xeRtlImageXexHeaderField(uint32_t xex_header_base_ptr,
                                  uint32_t image_field) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // PVOID
  // PVOID XexHeaderBase
  // DWORD ImageField

  // NOTE: this is totally faked!
  // We set the XexExecutableModuleHandle pointer to a block that has at offset
  // 0x58 a pointer to our XexHeaderBase. If the value passed doesn't match
  // then die.
  // The only ImageField I've seen in the wild is
  // 0x20401 (XEX_HEADER_DEFAULT_HEAP_SIZE), so that's all we'll support.

  XModule* module = NULL;

  // TODO(benvanik): use xex_header_base to dereference this.
  // Right now we are only concerned with games making this call on their main
  // module, so this hack is fine.
  module = state->GetExecutableModule();

  const xe_xex2_header_t* xex_header = module->xex_header();
  for (size_t n = 0; n < xex_header->header_count; n++) {
    if (xex_header->headers[n].key == image_field) {
      module->Release();
      return xex_header->headers[n].value;
    }
  }

  module->Release();
  return 0;
}


SHIM_CALL RtlImageXexHeaderField_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t xex_header_base    = SHIM_GET_ARG_32(0);
  uint32_t image_field        = SHIM_GET_ARG_32(1);

  // NOTE: this is totally faked!
  // We set the XexExecutableModuleHandle pointer to a block that has at offset
  // 0x58 a pointer to our XexHeaderBase. If the value passed doesn't match
  // then die.
  // The only ImageField I've seen in the wild is
  // 0x20401 (XEX_HEADER_DEFAULT_HEAP_SIZE), so that's all we'll support.

  XELOGD(
      "RtlImageXexHeaderField(%.8X, %.8X)",
      xex_header_base, image_field);

  uint32_t result = xeRtlImageXexHeaderField(xex_header_base, image_field);
  SHIM_SET_RETURN(result);
}


// Unfortunately the Windows RTL_CRITICAL_SECTION object is bigger than the one
// on the 360 (32b vs. 28b). This means that we can't do in-place splatting of
// the critical sections. Also, the 360 never calls RtlDeleteCriticalSection
// so we can't clean up the native handles.
//
// Because of this, we reimplement it poorly. Hooray.
// We have 28b to work with so we need to be careful. We map our struct directly
// into guest memory, as it should be opaque and so long as our size is right
// the user code will never know.
//
// Ref: http://msdn.microsoft.com/en-us/magazine/cc164040.aspx
// Ref: http://svn.reactos.org/svn/reactos/trunk/reactos/lib/rtl/critical.c?view=markup


// This structure tries to match the one on the 360 as best I can figure out.
// Unfortunately some games have the critical sections pre-initialized in
// their embedded data and InitializeCriticalSection will never be called.
namespace {
#pragma pack(push, 1)
typedef struct {
  uint8_t     unknown00;
  uint8_t     spin_count_div_256; // * 256
  uint8_t     __padding[6];
  //uint32_t    unknown04; // maybe the handle to the event?
  uint32_t    unknown08;          // head of queue, pointing to this offset
  uint32_t    unknown0C;          // tail of queue?
  int32_t     lock_count;         // -1 -> 0 on first lock 0x10
  uint32_t    recursion_count;    //  0 -> 1 on first lock 0x14
  uint32_t    owning_thread_id;   // 0 unless locked 0x18
} X_RTL_CRITICAL_SECTION;
#pragma pack(pop)
}

XEASSERTSTRUCTSIZE(X_RTL_CRITICAL_SECTION, 28);

void xeRtlInitializeCriticalSection(uint32_t cs_ptr) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // VOID
  // _Out_  LPCRITICAL_SECTION lpCriticalSection

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)IMPL_MEM_ADDR(cs_ptr);
  cs->unknown00           = 1;
  cs->spin_count_div_256  = 0;
  cs->lock_count          = -1;
  cs->recursion_count     = 0;
  cs->owning_thread_id    = 0;
}


SHIM_CALL RtlInitializeCriticalSection_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlInitializeCriticalSection(%.8X)", cs_ptr);

  xeRtlInitializeCriticalSection(cs_ptr);
}


X_STATUS xeRtlInitializeCriticalSectionAndSpinCount(
    uint32_t cs_ptr, uint32_t spin_count) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // NTSTATUS
  // _Out_  LPCRITICAL_SECTION lpCriticalSection,
  // _In_   DWORD dwSpinCount

  // Spin count is rouned up to 256 intervals then packed in.
  //uint32_t spin_count_div_256 = (uint32_t)floor(spin_count / 256.0f + 0.5f);
  uint32_t spin_count_div_256 = (spin_count + 255) >> 8;
  if (spin_count_div_256 > 255) {
    spin_count_div_256 = 255;
  }

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)IMPL_MEM_ADDR(cs_ptr);
  cs->unknown00           = 1;
  cs->spin_count_div_256  = spin_count_div_256;
  cs->lock_count          = -1;
  cs->recursion_count     = 0;
  cs->owning_thread_id    = 0;

  return X_STATUS_SUCCESS;
}


SHIM_CALL RtlInitializeCriticalSectionAndSpinCount_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);
  uint32_t spin_count = SHIM_GET_ARG_32(1);

  XELOGD("RtlInitializeCriticalSectionAndSpinCount(%.8X, %d)",
         cs_ptr, spin_count);

  X_STATUS result = xeRtlInitializeCriticalSectionAndSpinCount(
      cs_ptr, spin_count);
  SHIM_SET_RETURN(result);
}


// TODO(benvanik): remove the need for passing in thread_id.
void xeRtlEnterCriticalSection(uint32_t cs_ptr, uint32_t thread_id) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // VOID
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)IMPL_MEM_ADDR(cs_ptr);

  uint32_t spin_wait_remaining = cs->spin_count_div_256 * 256;
spin:
  if (xe_atomic_inc_32(&cs->lock_count)) {
    // If this thread already owns the CS increment the recursion count.
    if (cs->owning_thread_id == thread_id) {
      cs->recursion_count++;
      return;
    }

    // Thread was locked - spin wait.
    if (spin_wait_remaining) {
      spin_wait_remaining--;
      goto spin;
    }

    // All out of spin waits, create a full waiter.
    // TODO(benvanik): contention - do a real wait!
    //XELOGE("RtlEnterCriticalSection tried to really lock!");
    spin_wait_remaining = 1; // HACK: spin forever
    Sleep(1);
    goto spin;
  }

  // Now own the lock.
  cs->owning_thread_id  = thread_id;
  cs->recursion_count   = 1;
}


SHIM_CALL RtlEnterCriticalSection_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlEnterCriticalSection(%.8X)", cs_ptr);

  const uint8_t* thread_state_block = ppc_state->membase + ppc_state->r[13];
  uint32_t thread_id = XThread::GetCurrentThreadId(thread_state_block);

  xeRtlEnterCriticalSection(cs_ptr, thread_id);
}


// TODO(benvanik): remove the need for passing in thread_id.
uint32_t xeRtlTryEnterCriticalSection(uint32_t cs_ptr, uint32_t thread_id) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // DWORD
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)IMPL_MEM_ADDR(cs_ptr);

  if (xe_atomic_cas_32(-1, 0, &cs->lock_count)) {
    // Able to steal the lock right away.
    cs->owning_thread_id  = thread_id;
    cs->recursion_count   = 1;
    return 1;
  } else if (cs->owning_thread_id == thread_id) {
    xe_atomic_inc_32(&cs->lock_count);
    ++cs->recursion_count;
    return 1;
  }

  return 0;
}


SHIM_CALL RtlTryEnterCriticalSection_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlTryEnterCriticalSection(%.8X)", cs_ptr);

  const uint8_t* thread_state_block = ppc_state->membase + ppc_state->r[13];
  uint32_t thread_id = XThread::GetCurrentThreadId(thread_state_block);

  uint32_t result = xeRtlTryEnterCriticalSection(cs_ptr, thread_id);
  SHIM_SET_RETURN(result);
}


void xeRtlLeaveCriticalSection(uint32_t cs_ptr) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // VOID
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)IMPL_MEM_ADDR(cs_ptr);

  // Drop recursion count - if we are still not zero'ed return.
  uint32_t recursion_count = --cs->recursion_count;
  if (recursion_count) {
    xe_atomic_dec_32(&cs->lock_count);
    return;
  }

  // Unlock!
  cs->owning_thread_id  = 0;
  if (xe_atomic_dec_32(&cs->lock_count) != -1) {
    // There were waiters - wake one of them.
    // TODO(benvanik): wake a waiter.
    XELOGE("RtlLeaveCriticalSection would have woken a waiter");
  }
}


SHIM_CALL RtlLeaveCriticalSection_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlLeaveCriticalSection(%.8X)", cs_ptr);

  xeRtlLeaveCriticalSection(cs_ptr);
}


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterRtlExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlCompareMemory, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlCompareMemoryUlong, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlFillMemoryUlong, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlInitAnsiString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlFreeAnsiString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlInitUnicodeString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlFreeUnicodeString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlUnicodeStringToAnsiString, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", _vsnprintf, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlNtStatusToDosError, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlImageXexHeaderField, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlInitializeCriticalSection, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlInitializeCriticalSectionAndSpinCount, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlEnterCriticalSection, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlTryEnterCriticalSection, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlLeaveCriticalSection, state);
}
