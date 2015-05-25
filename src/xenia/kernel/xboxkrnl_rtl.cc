/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xboxkrnl_rtl.h"

#include <algorithm>

#include "xenia/base/atomic.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/kernel/objects/xuser_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/util/xex2.h"

namespace xe {
namespace kernel {

// http://msdn.microsoft.com/en-us/library/ff561778
SHIM_CALL RtlCompareMemory_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t source1_ptr = SHIM_GET_ARG_32(0);
  uint32_t source2_ptr = SHIM_GET_ARG_32(1);
  uint32_t length = SHIM_GET_ARG_32(2);

  XELOGD("RtlCompareMemory(%.8X, %.8X, %d)", source1_ptr, source2_ptr, length);

  // SIZE_T
  // _In_  const VOID *Source1,
  // _In_  const VOID *Source2,
  // _In_  SIZE_T Length

  uint8_t* p1 = SHIM_MEM_ADDR(source1_ptr);
  uint8_t* p2 = SHIM_MEM_ADDR(source2_ptr);

  // Note that the return value is the number of bytes that match, so it's best
  // we just do this ourselves vs. using memcmp.
  // On Windows we could use the builtin function.

  uint32_t c = 0;
  for (uint32_t n = 0; n < length; n++, p1++, p2++) {
    if (*p1 == *p2) {
      c++;
    }
  }

  SHIM_SET_RETURN_64(c);
}

// http://msdn.microsoft.com/en-us/library/ff552123
SHIM_CALL RtlCompareMemoryUlong_shim(PPCContext* ppc_state,
                                     KernelState* state) {
  uint32_t source_ptr = SHIM_GET_ARG_32(0);
  uint32_t length = SHIM_GET_ARG_32(1);
  uint32_t pattern = SHIM_GET_ARG_32(2);

  XELOGD("RtlCompareMemoryUlong(%.8X, %d, %.8X)", source_ptr, length, pattern);

  // SIZE_T
  // _In_  PVOID Source,
  // _In_  SIZE_T Length,
  // _In_  ULONG Pattern

  if ((source_ptr % 4) || (length % 4)) {
    SHIM_SET_RETURN_64(0);
    return;
  }

  uint8_t* p = SHIM_MEM_ADDR(source_ptr);

  // Swap pattern.
  // TODO(benvanik): ensure byte order of pattern is correct.
  // Since we are doing byte-by-byte comparison we may not want to swap.
  // GET_ARG swaps, so this is a swap back. Ugly.
  const uint32_t pb32 = xe::byte_swap(pattern);
  const uint8_t* pb = (uint8_t*)&pb32;

  uint32_t c = 0;
  for (uint32_t n = 0; n < length; n++, p++) {
    if (*p == pb[n % 4]) {
      c++;
    }
  }

  SHIM_SET_RETURN_64(c);
}

// http://msdn.microsoft.com/en-us/library/ff552263
SHIM_CALL RtlFillMemoryUlong_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t length = SHIM_GET_ARG_32(1);
  uint32_t pattern = SHIM_GET_ARG_32(2);

  XELOGD("RtlFillMemoryUlong(%.8X, %d, %.8X)", destination_ptr, length,
         pattern);

  // VOID
  // _Out_  PVOID Destination,
  // _In_   SIZE_T Length,
  // _In_   ULONG Pattern

  // NOTE: length must be % 4, so we can work on uint32s.
  uint32_t* p = (uint32_t*)SHIM_MEM_ADDR(destination_ptr);

  // TODO(benvanik): ensure byte order is correct - we're writing back the
  // swapped arg value.

  uint32_t count = length >> 2;
  uint32_t native_pattern = xe::byte_swap(pattern);

  // TODO: unroll loop?

  for (uint32_t n = 0; n < count; n++, p++) {
    *p = native_pattern;
  }
}

SHIM_CALL RtlCompareString_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t string_1_ptr = SHIM_GET_ARG_32(0);
  uint32_t string_2_ptr = SHIM_GET_ARG_32(1);
  uint32_t case_insensitive = SHIM_GET_ARG_32(2);

  XELOGD("RtlCompareString(%.8X, %.8X, %d)", string_1_ptr, string_2_ptr,
         case_insensitive);

  auto string_1 = reinterpret_cast<const char*>(SHIM_MEM_ADDR(string_1_ptr));
  auto string_2 = reinterpret_cast<const char*>(SHIM_MEM_ADDR(string_2_ptr));
  int ret = case_insensitive ? strcasecmp(string_1, string_2)
                             : std::strcmp(string_1, string_2);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL RtlCompareStringN_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t string_1_ptr = SHIM_GET_ARG_32(0);
  uint32_t string_1_len = SHIM_GET_ARG_32(1);
  uint32_t string_2_ptr = SHIM_GET_ARG_32(2);
  uint32_t string_2_len = SHIM_GET_ARG_32(3);
  uint32_t case_insensitive = SHIM_GET_ARG_32(4);

  XELOGD("RtlCompareStringN(%.8X, %d, %.8X, %d, %d)", string_1_ptr,
         string_1_len, string_2_ptr, string_2_len, case_insensitive);

  auto string_1 = reinterpret_cast<const char*>(SHIM_MEM_ADDR(string_1_ptr));
  auto string_2 = reinterpret_cast<const char*>(SHIM_MEM_ADDR(string_2_ptr));

  if (string_1_len == 0xFFFF) {
    string_1_len = uint32_t(std::strlen(string_1));
  }
  if (string_2_len == 0xFFFF) {
    string_2_len = uint32_t(std::strlen(string_2));
  }
  auto len = std::min(string_1_len, string_2_len);

  int ret = case_insensitive ? strncasecmp(string_1, string_2, len)
                             : std::strncmp(string_1, string_2, len);

  SHIM_SET_RETURN_32(ret);
}

// typedef struct _STRING {
//   USHORT Length;
//   USHORT MaximumLength;
//   PCHAR  Buffer;
// } ANSI_STRING, *PANSI_STRING;

// http://msdn.microsoft.com/en-us/library/ff561918
SHIM_CALL RtlInitAnsiString_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t source_ptr = SHIM_GET_ARG_32(1);

  const char* source = source_ptr ? (char*)SHIM_MEM_ADDR(source_ptr) : nullptr;
  XELOGD("RtlInitAnsiString(%.8X, %.8X = %s)", destination_ptr, source_ptr,
         source ? source : "<null>");

  // VOID
  // _Out_     PANSI_STRING DestinationString,
  // _In_opt_  PCSZ SourceString

  if (source_ptr != 0) {
    uint16_t length = (uint16_t)strlen(source);
    SHIM_SET_MEM_16(destination_ptr + 0, length);
    SHIM_SET_MEM_16(destination_ptr + 2, length + 1);
  } else {
    SHIM_SET_MEM_16(destination_ptr + 0, 0);
    SHIM_SET_MEM_16(destination_ptr + 2, 0);
  }
  SHIM_SET_MEM_32(destination_ptr + 4, source_ptr);
}

// http://msdn.microsoft.com/en-us/library/ff561899
SHIM_CALL RtlFreeAnsiString_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t string_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlFreeAnsiString(%.8X)", string_ptr);

  // VOID
  // _Inout_  PANSI_STRING AnsiString

  uint32_t buffer = SHIM_MEM_32(string_ptr + 4);
  if (!buffer) {
    return;
  }
  uint32_t length = SHIM_MEM_16(string_ptr + 2);
  state->memory()->SystemHeapFree(buffer);

  SHIM_SET_MEM_16(string_ptr + 0, 0);
  SHIM_SET_MEM_16(string_ptr + 2, 0);
  SHIM_SET_MEM_32(string_ptr + 4, 0);
}

// typedef struct _UNICODE_STRING {
//   USHORT Length;
//   USHORT MaximumLength;
//   PWSTR  Buffer;
// } UNICODE_STRING, *PUNICODE_STRING;

// http://msdn.microsoft.com/en-us/library/ff561934
SHIM_CALL RtlInitUnicodeString_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t source_ptr = SHIM_GET_ARG_32(1);

  auto source = source_ptr
                    ? xe::load_and_swap<std::wstring>(SHIM_MEM_ADDR(source_ptr))
                    : L"";
  XELOGD("RtlInitUnicodeString(%.8X, %.8X = %ls)", destination_ptr, source_ptr,
         source.empty() ? L"<null>" : source.c_str());

  // VOID
  // _Out_     PUNICODE_STRING DestinationString,
  // _In_opt_  PCWSTR SourceString

  if (source.size()) {
    SHIM_SET_MEM_16(destination_ptr + 0,
                    static_cast<uint16_t>(source.size() * 2));
    SHIM_SET_MEM_16(destination_ptr + 2,
                    static_cast<uint16_t>((source.size() + 1) * 2));
    SHIM_SET_MEM_32(destination_ptr + 4, source_ptr);
  } else {
    SHIM_SET_MEM_16(destination_ptr + 0, 0);
    SHIM_SET_MEM_16(destination_ptr + 2, 0);
    SHIM_SET_MEM_32(destination_ptr + 4, 0);
  }
}

// http://msdn.microsoft.com/en-us/library/ff561903
SHIM_CALL RtlFreeUnicodeString_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t string_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlFreeUnicodeString(%.8X)", string_ptr);

  // VOID
  // _Inout_  PUNICODE_STRING UnicodeString

  uint32_t buffer = SHIM_MEM_32(string_ptr + 4);
  if (!buffer) {
    return;
  }
  uint32_t length = SHIM_MEM_16(string_ptr + 2);
  state->memory()->SystemHeapFree(buffer);

  SHIM_SET_MEM_16(string_ptr + 0, 0);
  SHIM_SET_MEM_16(string_ptr + 2, 0);
  SHIM_SET_MEM_32(string_ptr + 4, 0);
}

// http://msdn.microsoft.com/en-us/library/ff562969
SHIM_CALL RtlUnicodeStringToAnsiString_shim(PPCContext* ppc_state,
                                            KernelState* state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t source_ptr = SHIM_GET_ARG_32(1);
  uint32_t alloc_dest = SHIM_GET_ARG_32(2);

  XELOGD("RtlUnicodeStringToAnsiString(%.8X, %.8X, %d)", destination_ptr,
         source_ptr, alloc_dest);

  // NTSTATUS
  // _Inout_  PANSI_STRING DestinationString,
  // _In_     PCUNICODE_STRING SourceString,
  // _In_     BOOLEAN AllocateDestinationString

  std::wstring unicode_str = xe::load_and_swap<std::wstring>(
      SHIM_MEM_ADDR(SHIM_MEM_32(source_ptr + 4)));
  std::string ansi_str = xe::to_string(unicode_str);
  if (ansi_str.size() > 0xFFFF - 1) {
    SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER_2);
    return;
  }

  X_STATUS result = X_STATUS_SUCCESS;
  if (alloc_dest) {
    uint32_t buffer_ptr =
        state->memory()->SystemHeapAlloc(uint32_t(ansi_str.size() + 1));
    memcpy(SHIM_MEM_ADDR(buffer_ptr), ansi_str.data(), ansi_str.size() + 1);
    SHIM_SET_MEM_16(destination_ptr + 0,
                    static_cast<uint16_t>(ansi_str.size()));
    SHIM_SET_MEM_16(destination_ptr + 2,
                    static_cast<uint16_t>(ansi_str.size() + 1));
    SHIM_SET_MEM_32(destination_ptr + 4, static_cast<uint32_t>(buffer_ptr));
  } else {
    uint32_t buffer_capacity = SHIM_MEM_16(destination_ptr + 2);
    uint32_t buffer_ptr = SHIM_MEM_32(destination_ptr + 4);
    if (buffer_capacity < ansi_str.size() + 1) {
      // Too large - we just write what we can.
      result = X_STATUS_BUFFER_OVERFLOW;
      memcpy(SHIM_MEM_ADDR(buffer_ptr), ansi_str.data(), buffer_capacity - 1);
    } else {
      memcpy(SHIM_MEM_ADDR(buffer_ptr), ansi_str.data(), ansi_str.size() + 1);
    }
    SHIM_SET_MEM_8(buffer_ptr + buffer_capacity - 1, 0);  // \0
  }
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL RtlMultiByteToUnicodeN_shim(PPCContext* ppc_state,
                                      KernelState* state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t destination_len = SHIM_GET_ARG_32(1);
  uint32_t written_ptr = SHIM_GET_ARG_32(2);
  uint32_t source_ptr = SHIM_GET_ARG_32(3);
  uint32_t source_len = SHIM_GET_ARG_32(4);

  uint32_t copy_len = destination_len >> 1;
  copy_len = copy_len < source_len ? copy_len : source_len;

  // TODO: maybe use MultiByteToUnicode on Win32? would require swapping

  for (uint32_t i = 0; i < copy_len; i++) {
    xe::store_and_swap<uint16_t>(
        SHIM_MEM_ADDR(destination_ptr + i * 2),
        xe::load<uint8_t>(SHIM_MEM_ADDR(source_ptr + i)));
  }

  if (written_ptr != 0) {
    SHIM_SET_MEM_32(written_ptr, copy_len << 1);
  }

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL RtlUnicodeToMultiByteN_shim(PPCContext* ppc_state,
                                      KernelState* state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t destination_len = SHIM_GET_ARG_32(1);
  uint32_t written_ptr = SHIM_GET_ARG_32(2);
  uint32_t source_ptr = SHIM_GET_ARG_32(3);
  uint32_t source_len = SHIM_GET_ARG_32(4);

  uint32_t copy_len = source_len >> 1;
  copy_len = copy_len < destination_len ? copy_len : destination_len;

  // TODO: maybe use UnicodeToMultiByte on Win32?

  auto source = (uint16_t*)SHIM_MEM_ADDR(source_ptr);
  auto destination = (uint8_t*)SHIM_MEM_ADDR(destination_ptr);
  for (uint32_t i = 0; i < copy_len; i++) {
    uint16_t c = xe::byte_swap(*source++);
    *destination++ = c < 256 ? (uint8_t)c : '?';
  }

  if (written_ptr != 0) {
    SHIM_SET_MEM_32(written_ptr, copy_len);
  }

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL RtlImageXexHeaderField_shim(PPCContext* ppc_state,
                                      KernelState* state) {
  uint32_t xex_header_base = SHIM_GET_ARG_32(0);
  uint32_t image_field = SHIM_GET_ARG_32(1);

  // NOTE: this is totally faked!
  // We set the XexExecutableModuleHandle pointer to a block that has at offset
  // 0x58 a pointer to our XexHeaderBase. If the value passed doesn't match
  // then die.
  // The only ImageField I've seen in the wild is
  // 0x20401 (XEX_HEADER_DEFAULT_HEAP_SIZE), so that's all we'll support.

  XELOGD("RtlImageXexHeaderField(%.8X, %.8X)", xex_header_base, image_field);

  // PVOID
  // PVOID XexHeaderBase
  // DWORD ImageField

  // NOTE: this is totally faked!
  // We set the XexExecutableModuleHandle pointer to a block that has at offset
  // 0x58 a pointer to our XexHeaderBase. If the value passed doesn't match
  // then die.
  // The only ImageField I've seen in the wild is
  // 0x20401 (XEX_HEADER_DEFAULT_HEAP_SIZE), so that's all we'll support.

  // TODO(benvanik): use xex_header_base to dereference this.
  // Right now we are only concerned with games making this call on their main
  // module, so this hack is fine.
  auto module = state->GetExecutableModule();

  const xe_xex2_header_t* xex_header = module->xex_header();
  for (size_t n = 0; n < xex_header->header_count; n++) {
    if (xex_header->headers[n].key == image_field) {
      uint32_t value = xex_header->headers[n].value;
      SHIM_SET_RETURN_64(value);
      return;
    }
  }

  SHIM_SET_RETURN_64(0);
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
// Ref:
// http://svn.reactos.org/svn/reactos/trunk/reactos/lib/rtl/critical.c?view=markup

// This structure tries to match the one on the 360 as best I can figure out.
// Unfortunately some games have the critical sections pre-initialized in
// their embedded data and InitializeCriticalSection will never be called.
#pragma pack(push, 1)
struct X_RTL_CRITICAL_SECTION {
  uint8_t unknown00;
  uint8_t spin_count_div_256;  // * 256
  uint16_t __padding0;
  uint32_t __padding1;
  // uint32_t    unknown04; // maybe the handle to the event?
  uint32_t queue_head;        // head of queue, pointing to this offset
  uint32_t queue_tail;        // tail of queue?
  int32_t lock_count;         // -1 -> 0 on first lock 0x10
  uint32_t recursion_count;   //  0 -> 1 on first lock 0x14
  uint32_t owning_thread_id;  // 0 unless locked 0x18
};
#pragma pack(pop)
static_assert_size(X_RTL_CRITICAL_SECTION, 28);

void xeRtlInitializeCriticalSection(X_RTL_CRITICAL_SECTION* cs,
                                    uint32_t cs_ptr) {
  // VOID
  // _Out_  LPCRITICAL_SECTION lpCriticalSection

  cs->unknown00 = 1;
  cs->spin_count_div_256 = 0;
  cs->queue_head = cs_ptr + 8;
  cs->queue_tail = cs_ptr + 8;
  cs->lock_count = -1;
  cs->recursion_count = 0;
  cs->owning_thread_id = 0;
}

SHIM_CALL RtlInitializeCriticalSection_shim(PPCContext* ppc_state,
                                            KernelState* state) {
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlInitializeCriticalSection(%.8X)", cs_ptr);

  auto cs = (X_RTL_CRITICAL_SECTION*)SHIM_MEM_ADDR(cs_ptr);
  xeRtlInitializeCriticalSection(cs, cs_ptr);
}

X_STATUS xeRtlInitializeCriticalSectionAndSpinCount(X_RTL_CRITICAL_SECTION* cs,
                                                    uint32_t cs_ptr,
                                                    uint32_t spin_count) {
  // NTSTATUS
  // _Out_  LPCRITICAL_SECTION lpCriticalSection,
  // _In_   DWORD dwSpinCount

  // Spin count is rouned up to 256 intervals then packed in.
  // uint32_t spin_count_div_256 = (uint32_t)floor(spin_count / 256.0f + 0.5f);
  uint32_t spin_count_div_256 = (spin_count + 255) >> 8;
  if (spin_count_div_256 > 255) {
    spin_count_div_256 = 255;
  }

  cs->unknown00 = 1;
  cs->spin_count_div_256 = spin_count_div_256;
  cs->queue_head = cs_ptr + 8;
  cs->queue_tail = cs_ptr + 8;
  cs->lock_count = -1;
  cs->recursion_count = 0;
  cs->owning_thread_id = 0;

  return X_STATUS_SUCCESS;
}

SHIM_CALL RtlInitializeCriticalSectionAndSpinCount_shim(PPCContext* ppc_state,
                                                        KernelState* state) {
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);
  uint32_t spin_count = SHIM_GET_ARG_32(1);

  XELOGD("RtlInitializeCriticalSectionAndSpinCount(%.8X, %d)", cs_ptr,
         spin_count);

  auto cs = (X_RTL_CRITICAL_SECTION*)SHIM_MEM_ADDR(cs_ptr);
  X_STATUS result =
      xeRtlInitializeCriticalSectionAndSpinCount(cs, cs_ptr, spin_count);
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL RtlEnterCriticalSection_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  // VOID
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  // XELOGD("RtlEnterCriticalSection(%.8X)", cs_ptr);

  const uint8_t* pcr = SHIM_MEM_ADDR(ppc_state->r[13]);
  uint32_t thread_id = XThread::GetCurrentThreadId(pcr);

  auto cs = (X_RTL_CRITICAL_SECTION*)SHIM_MEM_ADDR(cs_ptr);

  uint32_t spin_wait_remaining = cs->spin_count_div_256 * 256;
spin:
  if (xe::atomic_inc(&cs->lock_count) != 0) {
    // If this thread already owns the CS increment the recursion count.
    if (cs->owning_thread_id == thread_id) {
      cs->recursion_count++;
      return;
    }
    xe::atomic_dec(&cs->lock_count);

    // Thread was locked - spin wait.
    if (spin_wait_remaining) {
      spin_wait_remaining--;
      goto spin;
    }

    // All out of spin waits, create a full waiter.
    // TODO(benvanik): contention - do a real wait!
    // XELOGE("RtlEnterCriticalSection tried to really lock!");
    spin_wait_remaining = 0;  // HACK: spin forever
    SwitchToThread();
    goto spin;
  }

  // Now own the lock.
  cs->owning_thread_id = thread_id;
  cs->recursion_count = 1;
}

SHIM_CALL RtlTryEnterCriticalSection_shim(PPCContext* ppc_state,
                                          KernelState* state) {
  // DWORD
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  // XELOGD("RtlTryEnterCriticalSection(%.8X)", cs_ptr);

  const uint8_t* pcr = SHIM_MEM_ADDR(ppc_state->r[13]);
  uint32_t thread_id = XThread::GetCurrentThreadId(pcr);

  auto cs = (X_RTL_CRITICAL_SECTION*)SHIM_MEM_ADDR(cs_ptr);

  uint32_t result = 0;
  if (xe::atomic_cas(-1, 0, &cs->lock_count)) {
    // Able to steal the lock right away.
    cs->owning_thread_id = thread_id;
    cs->recursion_count = 1;
    result = 1;
  } else if (cs->owning_thread_id == thread_id) {
    xe::atomic_inc(&cs->lock_count);
    ++cs->recursion_count;
    result = 1;
  }
  SHIM_SET_RETURN_64(result);
}

SHIM_CALL RtlLeaveCriticalSection_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  // VOID
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  // XELOGD("RtlLeaveCriticalSection(%.8X)", cs_ptr);

  auto cs = (X_RTL_CRITICAL_SECTION*)SHIM_MEM_ADDR(cs_ptr);

  // Drop recursion count - if we are still not zero'ed return.
  uint32_t recursion_count = --cs->recursion_count;
  if (recursion_count) {
    xe::atomic_dec(&cs->lock_count);
    return;
  }

  // Unlock!
  cs->owning_thread_id = 0;
  if (xe::atomic_dec(&cs->lock_count) != -1) {
    // There were waiters - wake one of them.
    // TODO(benvanik): wake a waiter.
    XELOGE("RtlLeaveCriticalSection would have woken a waiter");
  }
}

SHIM_CALL RtlTimeToTimeFields_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t time_ptr = SHIM_GET_ARG_32(0);
  uint32_t time_fields_ptr = SHIM_GET_ARG_32(1);

  XELOGD("RtlTimeToTimeFields(%.8X, %.8X)", time_ptr, time_fields_ptr);

  uint64_t time = SHIM_MEM_64(time_ptr);
  FILETIME ft;
  ft.dwHighDateTime = time >> 32;
  ft.dwLowDateTime = (uint32_t)time;

  SYSTEMTIME st;
  FileTimeToSystemTime(&ft, &st);

  SHIM_SET_MEM_16(time_fields_ptr + 0, st.wYear);
  SHIM_SET_MEM_16(time_fields_ptr + 2, st.wMonth);
  SHIM_SET_MEM_16(time_fields_ptr + 4, st.wDay);
  SHIM_SET_MEM_16(time_fields_ptr + 6, st.wHour);
  SHIM_SET_MEM_16(time_fields_ptr + 8, st.wMinute);
  SHIM_SET_MEM_16(time_fields_ptr + 10, st.wSecond);
  SHIM_SET_MEM_16(time_fields_ptr + 12, st.wMilliseconds);
}

SHIM_CALL RtlTimeFieldsToTime_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t time_fields_ptr = SHIM_GET_ARG_32(0);
  uint32_t time_ptr = SHIM_GET_ARG_32(1);

  XELOGD("RtlTimeFieldsToTime(%.8X, %.8X)", time_fields_ptr, time_ptr);

  SYSTEMTIME st;
  st.wYear = SHIM_MEM_16(time_fields_ptr + 0);
  st.wMonth = SHIM_MEM_16(time_fields_ptr + 2);
  st.wDay = SHIM_MEM_16(time_fields_ptr + 4);
  st.wHour = SHIM_MEM_16(time_fields_ptr + 6);
  st.wMinute = SHIM_MEM_16(time_fields_ptr + 8);
  st.wSecond = SHIM_MEM_16(time_fields_ptr + 10);
  st.wMilliseconds = SHIM_MEM_16(time_fields_ptr + 12);

  FILETIME ft;
  if (!SystemTimeToFileTime(&st, &ft)) {
    // set last error = ERROR_INVALID_PARAMETER
    SHIM_SET_RETURN_64(0);
    return;
  }

  uint64_t time = (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
  SHIM_SET_MEM_64(time_ptr, time);
  SHIM_SET_RETURN_64(1);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterRtlExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlCompareMemory, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlCompareMemoryUlong, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlFillMemoryUlong, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlCompareString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlCompareStringN, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlInitAnsiString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlFreeAnsiString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlInitUnicodeString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlFreeUnicodeString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlUnicodeStringToAnsiString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlMultiByteToUnicodeN, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlUnicodeToMultiByteN, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlTimeToTimeFields, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlTimeFieldsToTime, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlImageXexHeaderField, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlInitializeCriticalSection, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlInitializeCriticalSectionAndSpinCount,
                   state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlEnterCriticalSection, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlTryEnterCriticalSection, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlLeaveCriticalSection, state);
}
